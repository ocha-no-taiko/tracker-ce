#include "q4/q4.h"

#include <stdio.h>
#include <string.h>

#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "Platform.h"
#include "ThemeManager.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interface.h"
#include "core/songTimer.h"
#include "q4/q4Screen.h"

cQ4 q4;
static cQ4* Q4 = &q4;

extern Q4ScreenData q4ScreenData; // gQ4.cpp
static Q4ScreenData& D = q4ScreenData;

//==============================================================================
// Incoming USB MIDI, queued by the receive handlers (they may run from the
// audio callback) and handled in update(), as on the Q.T.R.L. screen.
namespace
{
struct MidiEvent
{
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

constexpr uint8_t kQueueSize = 64; // power of two
MidiEvent s_queue[kQueueSize];
volatile uint8_t s_queueHead = 0;
volatile uint8_t s_queueTail = 0;
volatile bool s_active = false;

inline void compilerBarrier()
{
    __asm volatile("" ::: "memory");
}

// Complete SysEx messages from Live. A full page with its waveform is about
// 30 messages; the slots live in external RAM.
constexpr uint16_t kSysExSlots = 256; // power of two
constexpr uint8_t kSysExLength = 160;
struct SysExMessage
{
    uint8_t length;
    uint8_t data[kSysExLength];
};
__NOINIT(EXTERNAL_RAM) SysExMessage s_sysEx[kSysExSlots];
volatile uint16_t s_sysExHead = 0;
volatile uint16_t s_sysExTail = 0;
__NOINIT(EXTERNAL_RAM) SysExMessage s_sysExPart;
bool s_sysExTooLong = false;

constexpr uint8_t kSysExHeader[4] = { 0xF0, 0x7D, 0x51, 0x34 };
constexpr uint32_t kNoticeMs = 4000;
constexpr uint8_t kPadColumns = 12;
constexpr uint8_t kPadRows = 4;
constexpr uint8_t kLedPad = 2;
constexpr uint8_t kLedC = 8;
constexpr uint8_t kLedPressed = 31;

bool shiftHeld()
{
    return mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
}

uint8_t splitTabs(char* text, char** fields, uint8_t maxFields)
{
    uint8_t n = 0;
    fields[n++] = text;
    for (char* p = text; *p && n < maxFields; p++)
    {
        if (*p == '\t')
        {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    snprintf(dst, N, "%s", src);
}

inline uint16_t read14(const uint8_t* p)
{
    return (uint16_t)(((p[0] & 0x7F) << 7) | (p[1] & 0x7F));
}

// Text of a SysEx from offset to the F7 (printable ASCII and tabs only).
void readText(const uint8_t* data, uint16_t length, uint16_t offset, char* out, uint16_t size)
{
    uint16_t n = 0;
    for (uint16_t i = offset; i + 1 < length && n + 1u < size; i++)
    {
        uint8_t c = data[i];
        out[n++] = (c == '\t' || (c >= 0x20 && c < 0x7F)) ? (char)c : '?';
    }
    out[n] = '\0';
}

bool isDeviceTab(uint8_t tab)
{
    return tab <= q4TabMod;
}
} // namespace

bool q4IsActive()
{
    return s_active;
}

void q4ReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2)
{
    if (channel != q4Config.controlChannel)
        return;
    uint8_t head = s_queueHead;
    uint8_t next = (head + 1) & (kQueueSize - 1);
    if (next == s_queueTail)
        return; // full: drop
    s_queue[head] = { status, data1, data2 };
    compilerBarrier();
    s_queueHead = next;
}

void q4ReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete)
{
    for (uint16_t i = 0; i < length; i++)
    {
        if (s_sysExPart.length < kSysExLength)
            s_sysExPart.data[s_sysExPart.length++] = data[i];
        else
            s_sysExTooLong = true;
    }
    if (!complete)
        return;

    bool ours = !s_sysExTooLong && s_sysExPart.length >= 6 &&
                memcmp(s_sysExPart.data, kSysExHeader, sizeof(kSysExHeader)) == 0;
    uint16_t head = s_sysExHead;
    uint16_t next = (head + 1) & (kSysExSlots - 1);
    if (ours && next != s_sysExTail)
    {
        s_sysEx[head] = s_sysExPart;
        compilerBarrier();
        s_sysExHead = next;
    }
    s_sysExPart.length = 0;
    s_sysExTooLong = false;
}

//==============================================================================
static void functButton(uint8_t button, uint8_t state)
{
    Q4->onButton(button, state);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    Q4->onPad(pad, state);
}

static void functJog(int16_t value)
{
    Q4->onJog(value);
}

//==============================================================================
void cQ4::start([[maybe_unused]] uint32_t options)
{
    q4ConfigLoad();

    if (!sequencer.isStop())
        sequencer.stop();

    D.cfg = &q4Config;
    q4ScreenReset(D);
    if (D.tab >= q4TabCount || D.tab == q4TabQuit)
        D.tab = q4TabMaterial;
    D.cursor = 0;
    D.helpScroll = 0;
    for (uint8_t i = 0; i < 8; i++)
        copyText(D.labels[i], q4Config.tabLabels[i]);
    lastDeviceTab = isDeviceTab(D.tab) ? D.tab : (uint8_t)q4TabMaterial;
    modHeld = false;
    modUsed = false;
    pressedCell = -1;
    trigChannel = -1;
    memset(padDown, 0, sizeof(padDown));
    memset(padLit, 0, sizeof(padLit));
    padsHeld = 0;

    songTimer.hide();
    loadScreenFont();

    for (uint8_t i = 0; i < interfaceButtonsCount; i++)
        ip->setButtonObj(i, functButton);
    ip->setPotObj(interfacePot0, functJog, false);
    ip->setPadsGlobal(functPads);

    s_queueTail = s_queueHead;
    s_sysExTail = s_sysExHead;
    s_sysExPart.length = 0;
    s_sysExTooLong = false;
    s_active = true;

    padsBacklight.clearGlobalPads(1, 1, 0);
    showScreen();
    showPadLeds();

    // Tells Live to send the page; then which tab and track are shown.
    sendCc(q4Config.helloCc, 127);
    sendCc(q4CcTrack, D.track);
    if (isDeviceTab(D.tab))
        sendCc(q4CcTab, D.tab);
    moduleRefresh = 1;
}

void cQ4::stop()
{
    allNotesOff();
    if (modHeld)
        sendCc(q4CcMapMode, 0);
    sendCc(q4Config.helloCc, 0);
    s_active = false;
    padsBacklight.clearGlobalPads(1, 1, 0);
    restoreUiFont();
    songTimer.show();
    moduleRefresh = 0;
}

// Q4.ptfont replaces the UI font while the screen is open.
void cQ4::loadScreenFont()
{
    ownFont = false;
    fontSwapped = false;
    const char* path = q4Config.fontPath;
    FileSystem* fs = platform->getFileSystem();
    if (path[0] == '\0' || fs == nullptr || !fs->isSdCardInserted() || !fs->exists(path))
        return;

    fontSwapped = true;
    ownFont = display.loadCustomFontFromSD(fs, path);
    if (!ownFont)
        restoreUiFont();
}

void cQ4::restoreUiFont()
{
    if (!fontSwapped)
        return;
    fontSwapped = false;
    ownFont = false;
    ThemeManager::applyFont(ThemeManager::getCurrentFontIndex());
}

void cQ4::update()
{
    for (uint8_t n = 0; n < kQueueSize && s_queueTail != s_queueHead; n++)
    {
        uint8_t tail = s_queueTail;
        MidiEvent ev = s_queue[tail];
        compilerBarrier();
        s_queueTail = (tail + 1) & (kQueueSize - 1);
        handleIncoming(ev.status, ev.data1, ev.data2);
    }
    for (uint16_t n = 0; n < 64 && s_sysExTail != s_sysExHead; n++)
    {
        uint16_t tail = s_sysExTail;
        compilerBarrier();
        handleSysEx(s_sysEx[tail].data, s_sysEx[tail].length);
        compilerBarrier();
        s_sysExTail = (tail + 1) & (kSysExSlots - 1);
    }

    if (pageChanged)
    {
        pageChanged = false;
        fixCursor();
    }

    if (D.notice[0] != '\0' && noticeAge > kNoticeMs)
    {
        D.notice[0] = '\0';
        dirty |= 1 << q4ScreenHeader;
    }

    if (dirty)
    {
        for (uint8_t part = 0; part < q4ScreenPartCount; part++)
        {
            if (dirty & (1 << part))
                refreshPart(part);
        }
        dirty = 0;
        display.synchronizeRefresh();
    }
}

//==============================================================================
void cQ4::sendCc(uint8_t cc, uint8_t value)
{
    if (cc > 127)
        return;
    platform->getUsbMidi()->sendControlChange(cc, value > 127 ? 127 : value, q4Config.controlChannel);
}

void cQ4::sendDelta(uint8_t cc, int16_t steps)
{
    if (steps > 63)
        steps = 63;
    if (steps < -63)
        steps = -63;
    sendCc(cc, (uint8_t)(64 + steps));
}

void cQ4::handleIncoming(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (status == 0xB0 && data1 == q4Config.helloCc && data2 > 0)
    {
        // The script (re)started: answer so it sends the page again.
        sendCc(q4Config.helloCc, 127);
        sendCc(q4CcTrack, D.track);
        if (isDeviceTab(D.tab))
            sendCc(q4CcTab, D.tab);
    }
}

// F0 7D 51 34 <command> <data...> F7 (see Q4SysEx)
void cQ4::handleSysEx(const uint8_t* data, uint16_t length)
{
    if (length < 6 || data[length - 1] != 0xF7)
        return;
    const uint8_t command = data[4];
    char text[kSysExLength];
    char* f[3];

    if (!D.connected)
    {
        D.connected = true;
        dirty |= 1 << q4ScreenHeader;
    }

    switch (command)
    {
        case q4SysExPage:
        {
            if (length < 13)
                break;
            const uint8_t tab = data[6];
            const uint8_t page = data[7];
            if (!D.pageValid || D.pageTab != tab || D.page != page || D.slot != data[9] || D.track != data[5])
                pageChanged = true;
            D.track = data[5];
            D.pageTab = tab;
            D.page = page;
            D.pageCount = data[8] ? data[8] : 1;
            D.slot = data[9];
            D.pageFlags = data[10];
            D.panel = data[11];
            readText(data, length, 12, text, sizeof(text));
            copyText(D.pageTitle, text);
            D.pageValid = true;
            // Live follows the tab it was asked for; on a device tab show it.
            if (isDeviceTab(D.tab) && isDeviceTab(tab) && D.tab != tab && !modHeld)
            {
                D.tab = tab;
                lastDeviceTab = tab;
                dirty |= 1 << q4ScreenBackground | 1 << q4ScreenHeader | 1 << q4ScreenLabels;
            }
            if (pageChanged)
                memset(D.cells, 0, sizeof(D.cells));
            dirty |= 1 << q4ScreenLeft | 1 << q4ScreenRight;
            break;
        }

        case q4SysExCell:
        {
            if (length < 14 || data[5] >= Q4ScreenData::kCells)
                break;
            Q4ScreenData::Cell& cell = D.cells[data[5]];
            cell.kind = data[6];
            cell.flags = data[7];
            cell.value = read14(data + 8);
            cell.mod = (int16_t)read14(data + 10) - 8192;
            cell.icon = data[12];
            readText(data, length, 13, text, sizeof(text));
            uint8_t count = splitTabs(text, f, 3);
            copyText(cell.label, f[0]);
            copyText(cell.text, count > 1 ? f[1] : "");
            copyText(cell.mode, count > 2 ? f[2] : "");
            pageChanged = true; // re-check the cursor
            dirty |= 1 << q4ScreenRight;
            break;
        }

        case q4SysExValue:
        {
            if (length < 12 || data[5] >= Q4ScreenData::kCells)
                break;
            Q4ScreenData::Cell& cell = D.cells[data[5]];
            cell.flags = data[6];
            cell.value = read14(data + 7);
            cell.mod = (int16_t)read14(data + 9) - 8192;
            readText(data, length, 11, text, sizeof(text));
            uint8_t count = splitTabs(text, f, 2);
            copyText(cell.text, f[0]);
            copyText(cell.mode, count > 1 ? f[1] : "");
            dirty |= 1 << q4ScreenRight;
            break;
        }

        case q4SysExStatus:
        {
            if (length < 13)
                break;
            D.track = data[5];
            D.scene = read14(data + 6);
            D.bpmTenths = (uint16_t)(((data[8] & 0x7F) << 14) | ((data[9] & 0x7F) << 7) | (data[10] & 0x7F));
            D.statusFlags = data[11];
            readText(data, length, 12, text, sizeof(text));
            copyText(D.sampleName, text);
            if (isDeviceTab(D.tab))
                dirty |= 1 << q4ScreenLeft;
            break;
        }

        case q4SysExWaveBegin:
        {
            if (length < 8)
                break;
            uint16_t count = read14(data + 5);
            D.waveCount = count > Q4ScreenData::kWaveMax ? Q4ScreenData::kWaveMax : count;
            memset(D.waveMin, 64, sizeof(D.waveMin));
            memset(D.waveMax, 64, sizeof(D.waveMax));
            if (D.tab == q4TabMaterial)
                dirty |= 1 << q4ScreenLeft;
            break;
        }

        case q4SysExWaveData:
        {
            if (length < 8)
                break;
            uint16_t column = read14(data + 5);
            for (uint16_t i = 7; i + 2 < length && column < Q4ScreenData::kWaveMax; i += 2, column++)
            {
                D.waveMin[column] = data[i] & 0x7F;
                D.waveMax[column] = data[i + 1] & 0x7F;
            }
            if (D.tab == q4TabMaterial)
                dirty |= 1 << q4ScreenLeft;
            break;
        }

        case q4SysExMarkers:
        {
            if (length < 14)
                break;
            D.markStart = read14(data + 5);
            D.markEnd = read14(data + 7);
            D.markLoop = read14(data + 9);
            D.playhead = read14(data + 11);
            D.sliceCount = 0;
            for (uint16_t i = 13; i + 2 < length && D.sliceCount < Q4ScreenData::kMaxSlices; i += 2)
                D.slices[D.sliceCount++] = read14(data + i);
            if (D.tab == q4TabMaterial)
                dirty |= 1 << q4ScreenLeft;
            break;
        }

        case q4SysExNotice:
            readText(data, length, 5, text, sizeof(text));
            copyText(D.notice, text);
            noticeAge = 0;
            dirty |= 1 << q4ScreenHeader;
            break;

        default: break;
    }
}

//==============================================================================
void cQ4::onButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress && state != buttonRelease)
        return;
    const Q4Config& cfg = q4Config;
    const bool press = (state == buttonPress);
    const bool shift = shiftHeld();

    // MOD (screen button 6): hold = mapping mode, tap = MOD tab
    if (button == interfaceButton0 + q4TabMod && D.tab != q4TabQuit)
    {
        if (press)
        {
            modHeld = true;
            modUsed = false;
            D.mapping = true;
            sendCc(q4CcMapMode, 127);
        }
        else if (modHeld)
        {
            modHeld = false;
            D.mapping = false;
            sendCc(q4CcMapMode, 0);
            if (!modUsed)
                pressTab(q4TabMod, false);
        }
        dirty |= 1 << q4ScreenHeader | 1 << q4ScreenRight;
        return;
    }

    if (button == cfg.pressButton)
    {
        pressCell(press);
        return;
    }
    if (!press)
        return;

    if (shift && button == cfg.exitButton)
    {
        exitScreen();
        return;
    }

    if (button >= interfaceButton0 && button <= interfaceButton7)
    {
        const uint8_t tab = button - interfaceButton0;
        if (D.tab == q4TabQuit)
        {
            if (tab == 6)
                setTab(lastDeviceTab); // Cancel
            else if (tab == 7)
                exitScreen();
            return;
        }
        pressTab(tab, shift);
        return;
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        if (button == cfg.trackButtons[i])
        {
            D.track = i;
            sendCc(q4CcTrack, i);
            dirty |= 1 << q4ScreenLeft;
            return;
        }
    }

    if (button == cfg.resetButton)
    {
        if (isDeviceTab(D.tab))
            sendCc(q4CcReset, D.cursor);
        return;
    }
    if (button == cfg.playButton)
    {
        sendCc(q4CcPlay, 127);
        return;
    }
    if (button == cfg.recordButton)
    {
        sendCc(q4CcRecord, 127);
        return;
    }

    switch (button)
    {
        case interfaceButtonLeft:
            if (D.tab == q4TabHelp)
                moveHelp(-1, 0);
            else
                moveCursor(-1, 0);
            break;
        case interfaceButtonRight:
            if (D.tab == q4TabHelp)
                moveHelp(1, 0);
            else
                moveCursor(1, 0);
            break;
        case interfaceButtonUp:
            if (D.tab == q4TabHelp)
                moveHelp(0, -1);
            else
                moveCursor(0, -1);
            break;
        case interfaceButtonDown:
            if (D.tab == q4TabHelp)
                moveHelp(0, 1);
            else
                moveCursor(0, 1);
            break;
        default: break;
    }
}

void cQ4::pressTab(uint8_t tab, bool shift)
{
    if (tab == q4TabHelp || tab == q4TabQuit)
    {
        if (!shift)
            setTab(tab);
        return;
    }
    if (modHeld)
        modUsed = true;
    if (shift)
    {
        if (tab < q4TabMod)
            sendCc(q4CcDevice, tab);
        if (D.tab != tab)
            setTab(tab);
        return;
    }
    // On the tab already: Live turns the page (or the MOD slot).
    sendCc(q4CcTab, tab);
    if (D.tab != tab)
        setTab(tab);
}

void cQ4::setTab(uint8_t tab)
{
    if (tab >= q4TabCount)
        return;
    D.tab = tab;
    if (isDeviceTab(tab))
        lastDeviceTab = tab;
    if (tab == q4TabHelp)
        D.helpScroll = 0;
    if (isDeviceTab(tab) && D.pageValid && D.pageTab != tab)
        D.cursor = 0;
    refreshAll();
    display.synchronizeRefresh();
}

// Arrows on a device page: the 4 x 3 grid, skipping empty cells.
void cQ4::moveCursor(int8_t columns, int8_t rows)
{
    if (!isDeviceTab(D.tab))
        return;
    int8_t col = D.cursor % Q4ScreenData::kColumns;
    int8_t row = D.cursor / Q4ScreenData::kColumns;
    const int8_t rowCount = Q4ScreenData::kCells / Q4ScreenData::kColumns;
    for (uint8_t tries = 0; tries < Q4ScreenData::kCells; tries++)
    {
        col += columns;
        row += rows;
        if (col < 0 || col >= Q4ScreenData::kColumns || row < 0 || row >= rowCount)
            return;
        uint8_t index = row * Q4ScreenData::kColumns + col;
        if (D.cells[index].kind != q4CellEmpty || !D.pageValid)
        {
            D.cursor = index;
            sendCursor();
            dirty |= 1 << q4ScreenRight;
            return;
        }
    }
}

void cQ4::sendCursor()
{
    sendCc(q4CcCursor, D.cursor);
}

// After a new page: keep the cursor on a cell that is there.
void cQ4::fixCursor()
{
    if (D.cursor < Q4ScreenData::kCells && D.cells[D.cursor].kind != q4CellEmpty)
    {
        sendCursor();
        return;
    }
    for (uint8_t i = 0; i < Q4ScreenData::kCells; i++)
    {
        if (D.cells[i].kind != q4CellEmpty)
        {
            D.cursor = i;
            sendCursor();
            dirty |= 1 << q4ScreenRight;
            return;
        }
    }
}

void cQ4::moveHelp(int8_t columns, int8_t rows)
{
    int8_t col = D.helpPage % 3 + columns;
    int8_t row = D.helpPage / 3 + rows;
    if (col < 0 || col > 2 || row < 0 || row > 2)
        return;
    D.helpPage = row * 3 + col;
    D.helpScroll = 0;
    dirty |= 1 << q4ScreenLeft | 1 << q4ScreenRight;
}

// The press button (Insert): an action cell is pressed (and released); on a
// parameter it changes the mode, or in mapping mode the polarity.
void cQ4::pressCell(bool press)
{
    if (!isDeviceTab(D.tab) || D.cursor >= Q4ScreenData::kCells)
        return;
    const Q4ScreenData::Cell& cell = D.cells[D.cursor];
    if (cell.kind == q4CellAction)
    {
        if (press)
        {
            pressedCell = (int8_t)D.cursor;
            sendCc(q4CcPress, D.cursor);
            if (cell.flags & q4FlagNoteTrig)
            {
                trigChannel = (int8_t)(D.track + 1);
                platform->getUsbMidi()->sendNoteOn(q4Config.trigNote, q4Config.padVelocity, trigChannel);
            }
        }
        else if (pressedCell >= 0)
        {
            sendCc(q4CcRelease, (uint8_t)pressedCell);
            pressedCell = -1;
            if (trigChannel > 0)
            {
                platform->getUsbMidi()->sendNoteOff(q4Config.trigNote, 0, trigChannel);
                trigChannel = -1;
            }
        }
        return;
    }
    if (!press || cell.kind == q4CellEmpty)
        return;
    if (modHeld)
    {
        modUsed = true;
        sendCc(q4CcMapPolarity, D.cursor);
    }
    else if (cell.flags & q4FlagModes)
    {
        sendCc(q4CcMode, D.cursor);
    }
}

void cQ4::onJog(int16_t value)
{
    if (value == 0)
        return;
    if (D.tab == q4TabHelp)
    {
        int32_t top = (int32_t)D.helpScroll + value;
        int32_t total = q4ScreenHelpLineCount(D);
        int32_t maxTop = total > q4ScreenHelpVisibleLines() ? total - q4ScreenHelpVisibleLines() : 0;
        if (top < 0)
            top = 0;
        if (top > maxTop)
            top = maxTop;
        D.helpScroll = (uint16_t)top;
        dirty |= 1 << q4ScreenRight;
        return;
    }
    if (!isDeviceTab(D.tab))
        return;
    if (modHeld)
    {
        modUsed = true;
        sendDelta(q4CcMapJog, value);
    }
    else
    {
        sendDelta(shiftHeld() ? q4CcJogFine : q4CcJog, value);
    }
}

//==============================================================================
// Pads: a chromatic keyboard on the track's channel (bottom-left = noteBase).
void cQ4::onPad(uint8_t pad, uint8_t state)
{
    if (pad >= kPadCount || (state != 0 && state != 1))
        return;
    UsbMidi* midi = platform->getUsbMidi();
    if (state == 1)
    {
        if (padDown[pad])
            return;
        const uint8_t row = kPadRows - 1 - pad / kPadColumns;
        const uint8_t note = q4Config.padNoteBase + row * kPadColumns + pad % kPadColumns;
        padDown[pad] = true;
        padNote[pad] = note;
        padChannel[pad] = D.track + 1;
        midi->sendNoteOn(note, q4Config.padVelocity, padChannel[pad]);
        if (padsHeld++ == 0)
            sendCc(q4CcGate, 127);
        padsBacklight.setFrontLayer(1, kLedPressed, pad);
        padLit[pad] = true;
    }
    else
    {
        if (!padDown[pad])
            return;
        padDown[pad] = false;
        midi->sendNoteOff(padNote[pad], 0, padChannel[pad]);
        if (padsHeld > 0 && --padsHeld == 0)
            sendCc(q4CcGate, 0);
        showPadLeds();
    }
}

void cQ4::allNotesOff()
{
    UsbMidi* midi = platform->getUsbMidi();
    for (uint8_t pad = 0; pad < kPadCount; pad++)
    {
        if (padDown[pad])
        {
            midi->sendNoteOff(padNote[pad], 0, padChannel[pad]);
            padDown[pad] = false;
        }
    }
    if (padsHeld > 0)
        sendCc(q4CcGate, 0);
    padsHeld = 0;
    if (trigChannel > 0)
    {
        midi->sendNoteOff(q4Config.trigNote, 0, trigChannel);
        trigChannel = -1;
    }
}

// Dim pads, the Cs a little brighter, held pads full.
void cQ4::showPadLeds()
{
    for (uint8_t pad = 0; pad < kPadCount; pad++)
    {
        uint8_t level = (pad % kPadColumns == 0) ? kLedC : kLedPad;
        if (padDown[pad])
            level = kLedPressed;
        padsBacklight.setFrontLayer(1, level, pad);
        padLit[pad] = true;
    }
}

void cQ4::exitScreen()
{
    hModule prev = mtInterface.getPreviousModule();
    if (prev == nullptr || prev == this)
    {
        uint8_t button = interfaceButtonPattern;
        eventFunct(eventSwitchModule, this, &button, 0);
    }
    else
    {
        eventFunct(eventSwitchToPreviousModule, this, 0, 0);
    }
}

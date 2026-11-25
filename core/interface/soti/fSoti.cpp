// The 1 MB flash is nearly full and this screen is not time-critical.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include "soti/soti.h"

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
#include "soti/sotiScreen.h"

cSoti soti;
static cSoti* SO = &soti;

extern SotiScreenData sotiScreenData; // gSoti.cpp
static SotiScreenData& D = sotiScreenData;

//==============================================================================
// Incoming USB MIDI, queued by the receive handlers (they may run from the
// audio callback) and handled in update(), as on the Q-4 screen.
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

// Complete SysEx messages from the server. The whole file list is one
// message per file (up to 256); the server paces them.
constexpr uint16_t kSysExSlots = 256; // power of two
constexpr uint8_t kSysExLength = 96;
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

constexpr uint8_t kSysExHeader[4] = { 0xF0, 0x7D, 0x53, 0x4F };
constexpr uint32_t kNoticeMs = 4000;
constexpr uint8_t kCols = SotiScreenData::kColumns;
constexpr uint8_t kParamRows = SotiConfig::kParams;

bool shiftHeld()
{
    return mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
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
} // namespace

bool sotiIsActive()
{
    return s_active;
}

void sotiReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2)
{
    if (channel != sotiConfig.controlChannel)
        return;
    uint8_t head = s_queueHead;
    uint8_t next = (head + 1) & (kQueueSize - 1);
    if (next == s_queueTail)
        return; // full: drop
    s_queue[head] = { status, data1, data2 };
    compilerBarrier();
    s_queueHead = next;
}

void sotiReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete)
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
    SO->onButton(button, state);
}

static void functPads([[maybe_unused]] uint8_t pad, [[maybe_unused]] uint8_t state, [[maybe_unused]] int16_t velo)
{
}

static void functJog(int16_t value)
{
    SO->onJog(value);
}

//==============================================================================
void cSoti::start([[maybe_unused]] uint32_t options)
{
    sotiConfigLoad();

    if (!sequencer.isStop())
        sequencer.stop();

    D.cfg = &sotiConfig;
    sotiScreenReset(D);
    if (D.focus > 1)
        D.focus = 0;
    if (D.controlCursor >= SotiScreenData::kValues)
        D.controlCursor = 0;
    D.quitArmed = false;

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
    sendHello();
    moduleRefresh = 1;
}

void cSoti::stop()
{
    sendCc(sotiConfig.helloCc, 0);
    s_active = false;
    restoreUiFont();
    songTimer.show();
    moduleRefresh = 0;
}

// Tells the server the screen is open (it answers with everything) and where
// the cursors are.
void cSoti::sendHello()
{
    sendCc(sotiConfig.helloCc, 127);
    sendCc(sotiCcFocus, D.focus);
    sendFileCursor();
    sendCc(sotiCcControl, D.controlCursor);
}

// Soti.ptfont replaces the UI font while the screen is open.
void cSoti::loadScreenFont()
{
    ownFont = false;
    fontSwapped = false;
    const char* path = sotiConfig.fontPath;
    FileSystem* fs = platform->getFileSystem();
    if (path[0] == '\0' || fs == nullptr || !fs->isSdCardInserted() || !fs->exists(path))
        return;

    fontSwapped = true;
    ownFont = display.loadCustomFontFromSD(fs, path);
    if (!ownFont)
        restoreUiFont();
}

void cSoti::restoreUiFont()
{
    if (!fontSwapped)
        return;
    fontSwapped = false;
    ownFont = false;
    ThemeManager::applyFont(ThemeManager::getCurrentFontIndex());
}

void cSoti::update()
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

    if (D.notice[0] != '\0' && noticeAge > kNoticeMs)
    {
        D.notice[0] = '\0';
        dirty |= 1 << sotiScreenHeader;
    }
    if (D.quitArmed && quitAge > sotiConfig.quitMs)
    {
        D.quitArmed = false;
        dirty |= 1 << sotiScreenLabels;
    }

    if (dirty)
    {
        for (uint8_t part = 0; part < sotiScreenPartCount; part++)
        {
            if (dirty & (1 << part))
                refreshPart(part);
        }
        dirty = 0;
        display.synchronizeRefresh();
    }
}

//==============================================================================
void cSoti::sendCc(uint8_t cc, uint8_t value)
{
    if (cc > 127)
        return;
    platform->getUsbMidi()->sendControlChange(cc, value > 127 ? 127 : value, sotiConfig.controlChannel);
}

void cSoti::sendDelta(uint8_t cc, int16_t steps)
{
    if (steps > 63)
        steps = 63;
    if (steps < -63)
        steps = -63;
    sendCc(cc, (uint8_t)(64 + steps));
}

void cSoti::sendFileCursor()
{
    sendCc(sotiCcFileHi, (D.fileCursor >> 7) & 0x7F);
    sendCc(sotiCcFileLo, D.fileCursor & 0x7F);
}

void cSoti::handleIncoming(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (status == 0xB0 && data1 == sotiConfig.helloCc && data2 > 0)
        sendHello(); // the server (re)started
}

// F0 7D 53 4F <command> <data...> F7 (see SotiSysEx)
void cSoti::handleSysEx(const uint8_t* data, uint16_t length)
{
    if (length < 6 || data[length - 1] != 0xF7)
        return;
    const uint8_t command = data[4];
    char text[kSysExLength];

    if (!D.connected)
    {
        D.connected = true;
        dirty |= 1 << sotiScreenHeader | 1 << sotiScreenLeft | 1 << sotiScreenRight;
    }

    switch (command)
    {
        case sotiSysExFiles:
        {
            if (length < 8)
                break;
            uint16_t count = read14(data + 5);
            D.fileCount = count > SotiScreenData::kMaxFiles ? SotiScreenData::kMaxFiles : count;
            memset(D.fileFlags, 0, sizeof(D.fileFlags));
            memset(D.fileNames, 0, sizeof(D.fileNames));
            if (D.fileCursor >= D.fileCount)
                setFileCursor(D.fileCount > 0 ? D.fileCount - 1 : 0);
            dirty |= 1 << sotiScreenLeft;
            break;
        }

        case sotiSysExFile:
        {
            if (length < 9)
                break;
            uint16_t index = read14(data + 5);
            if (index >= D.fileCount)
                break;
            D.fileFlags[index] = data[7];
            readText(data, length, 8, text, sizeof(text));
            copyText(D.fileNames[index], text);
            dirty |= 1 << sotiScreenLeft;
            break;
        }

        case sotiSysExValues:
        {
            if (length < 6 + 2 * SotiScreenData::kValues)
                break;
            for (uint8_t i = 0; i < SotiScreenData::kValues; i++)
                D.values[i] = read14(data + 5 + 2 * i);
            dirty |= 1 << sotiScreenRight | 1 << sotiScreenHeader;
            break;
        }

        case sotiSysExStatus:
        {
            if (length < 8)
                break;
            D.select = data[5] <= sotiSelectAll ? data[5] : (uint8_t)sotiSelectOne;
            D.statusFlags = data[6];
            readText(data, length, 7, text, sizeof(text));
            char* field = text;
            for (uint8_t s = 0; s < 3; s++)
            {
                char* tab = field ? strchr(field, '\t') : nullptr;
                if (tab)
                    *tab = '\0';
                copyText(D.slotNames[s], field ? field : "");
                field = tab ? tab + 1 : nullptr;
            }
            dirty |= 1 << sotiScreenHeader | 1 << sotiScreenRight | 1 << sotiScreenLabels;
            break;
        }

        case sotiSysExNotice:
            readText(data, length, 5, text, sizeof(text));
            copyText(D.notice, text);
            noticeAge = 0;
            dirty |= 1 << sotiScreenHeader;
            break;

        default: break;
    }
}

//==============================================================================
void cSoti::onButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    const SotiConfig& cfg = sotiConfig;
    const bool shift = shiftHeld();

    if (shift && button == cfg.exitButton)
    {
        exitScreen();
        return;
    }
    if (button >= interfaceButton0 && button <= interfaceButton7)
    {
        pressAction(button - interfaceButton0);
        return;
    }
    if (button == cfg.filesButton)
    {
        setFocus(0);
        return;
    }
    if (button == cfg.controlsButton)
    {
        setFocus(1);
        return;
    }
    if (button == cfg.resetButton)
    {
        sendCc(sotiCcControl, D.controlCursor);
        sendCc(sotiCcReset, 127);
        return;
    }
    if (button == cfg.playButton)
    {
        sendCc(sotiCcPlay, 127);
        return;
    }

    int8_t dx = 0;
    int8_t dy = 0;
    switch (button)
    {
        case interfaceButtonLeft: dx = -1; break;
        case interfaceButtonRight: dx = 1; break;
        case interfaceButtonUp: dy = -1; break;
        case interfaceButtonDown: dy = 1; break;
        default: return;
    }
    if (D.focus == 0)
        moveFile(dx, dy);
    else
        moveControl(dx, dy);
}

void cSoti::setFocus(uint8_t focus)
{
    if (D.focus == focus)
        return;
    D.focus = focus;
    sendCc(sotiCcFocus, focus);
    dirty |= 1 << sotiScreenLeft | 1 << sotiScreenRight | 1 << sotiScreenBackground;
}

// Screen buttons 1..8: Load into A / B / C / All, SLCT row / column / All, QUIT
void cSoti::pressAction(uint8_t action)
{
    if (action == 7)
    {
        if (D.quitArmed && quitAge <= sotiConfig.quitMs)
        {
            exitScreen();
            return;
        }
        D.quitArmed = true;
        quitAge = 0;
        dirty |= 1 << sotiScreenLabels;
        return;
    }
    if (D.quitArmed)
    {
        D.quitArmed = false;
        dirty |= 1 << sotiScreenLabels;
    }
    if (action <= 3)
    {
        if (D.fileCount == 0)
            return;
        sendFileCursor();
        sendCc(sotiCcLoad, action);
    }
    else
    {
        sendCc(sotiCcControl, D.controlCursor);
        sendCc(sotiCcSelect, action - 3);
    }
}

void cSoti::setFileCursor(int32_t index)
{
    if (D.fileCount == 0)
    {
        D.fileCursor = 0;
        D.fileTop = 0;
        return;
    }
    if (index < 0)
        index = 0;
    if (index >= D.fileCount)
        index = D.fileCount - 1;
    D.fileCursor = (uint16_t)index;
    // keep the cursor's row on the screen
    const uint16_t row = D.fileCursor / kCols;
    if (row < D.fileTop)
        D.fileTop = row;
    if (row >= D.fileTop + SotiScreenData::kFileRows)
        D.fileTop = row - SotiScreenData::kFileRows + 1;
    dirty |= 1 << sotiScreenLeft;
}

// Arrows on the files: the 3-column grid, scrolling by rows.
void cSoti::moveFile(int16_t columns, int16_t rows)
{
    if (D.fileCount == 0)
        return;
    int16_t col = D.fileCursor % kCols + columns;
    int32_t row = D.fileCursor / kCols + rows;
    if (col < 0 || col >= kCols || row < 0)
        return;
    int32_t index = row * kCols + col;
    if (index >= D.fileCount)
    {
        // down into a short last row: its last file
        if (rows > 0 && row * kCols < D.fileCount)
            index = D.fileCount - 1;
        else
            return;
    }
    setFileCursor(index);
    sendFileCursor();
}

void cSoti::moveControl(int8_t columns, int8_t rows)
{
    int8_t col = D.controlCursor % kCols + columns;
    int8_t row = D.controlCursor / kCols + rows;
    if (col < 0 || col >= kCols || row < 0 || row >= kParamRows)
        return;
    D.controlCursor = (uint8_t)(row * kCols + col);
    sendCc(sotiCcControl, D.controlCursor);
    dirty |= 1 << sotiScreenRight | 1 << sotiScreenHeader;
}

void cSoti::onJog(int16_t value)
{
    if (value == 0)
        return;
    if (D.focus == 0)
    {
        // the jog walks through the files one by one
        if (D.fileCount == 0)
            return;
        setFileCursor((int32_t)D.fileCursor + value);
        sendFileCursor();
        return;
    }
    sendCc(sotiCcControl, D.controlCursor);
    sendDelta(shiftHeld() ? sotiCcJogFine : sotiCcJog, value);
}

void cSoti::exitScreen()
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

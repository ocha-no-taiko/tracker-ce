#include "qtrl/qtrl.h"

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
#include "qtrl/qtrlScreen.h"

cQtrl qtrl;
static cQtrl* QT = &qtrl;

extern QtrlScreenData qtrlScreenData; // gQtrl.cpp
static QtrlScreenData& D = qtrlScreenData;

//==============================================================================
// Incoming USB MIDI, queued by the receive handlers (they may run from the
// audio callback) and handled in update(), as on the DAW Control screen.
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

// Complete SysEx messages from the server. A burst (state, log, chat) can be
// a few hundred messages, so the slots live in external RAM.
constexpr uint16_t kSysExSlots = 256; // power of two
constexpr uint8_t kSysExLength = 128;
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

constexpr uint8_t kSysExHeader[4] = { 0xF0, 0x7D, 0x51, 0x54 };
constexpr uint8_t kChunk = 60;              // text bytes per SysEx to the server
constexpr uint16_t kChatPendingLength = 2048; // longest answer kept
__NOINIT(EXTERNAL_RAM) char s_chatPending[kChatPendingLength + 1];

constexpr uint32_t kNoticeMs = 6000;
constexpr uint32_t kLogRequestDelayMs = 150; // wait for the arrows to settle

bool shiftHeld()
{
    return mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
}

// "a<TAB>b<TAB>..." -> fields (modifies text)
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

uint8_t parseState(char c)
{
    if (c == 'O')
        return qtrlStateOk;
    if (c == 'A')
        return qtrlStateAnomaly;
    return qtrlStateUnknown;
}

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    snprintf(dst, N, "%s", src);
}
} // namespace

bool qtrlIsActive()
{
    return s_active;
}

void qtrlReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2)
{
    if (channel != qtrlConfig.controlChannel)
        return;
    uint8_t head = s_queueHead;
    uint8_t next = (head + 1) & (kQueueSize - 1);
    if (next == s_queueTail)
        return; // full: drop
    s_queue[head] = { status, data1, data2 };
    compilerBarrier();
    s_queueHead = next;
}

void qtrlReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete)
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

    bool ours = !s_sysExTooLong && s_sysExPart.length >= 8 &&
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
    QT->onButton(button, state);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    QT->onPad(pad, state);
}

static void functJog(int16_t value)
{
    QT->onJog(value);
}

//==============================================================================
void cQtrl::start([[maybe_unused]] uint32_t options)
{
    qtrlConfigLoad();

    if (!sequencer.isStop())
        sequencer.stop();

    D.cfg = &qtrlConfig;
    qtrlScreenReset(D);
    D.tab = qtrlTabHome;
    D.input[0] = '\0';
    D.inputLength = 0;
    D.caps = false;
    D.shift = false;
    D.pressedKey = -1;
    for (uint8_t i = 0; i < 8; i++)
        copyText(D.labels[i], qtrlConfig.labels[i]);
    chatSpeaker[0] = '\0';
    chatPendingLength = 0;
    logRequestPending = false;
    followLog = true;
    memset(padLit, 0, sizeof(padLit));

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

    // Tells the server to send its full state.
    sendCc(qtrlConfig.helloCc, 127);
    moduleRefresh = 1;
}

void cQtrl::stop()
{
    sendCc(qtrlConfig.helloCc, 0);
    s_active = false;
    padsBacklight.clearGlobalPads(1, 1, 0);
    restoreUiFont();
    songTimer.show();
    moduleRefresh = 0;
}

// Qtrl.ptfont replaces the UI font while the screen is open.
void cQtrl::loadScreenFont()
{
    ownFont = false;
    fontSwapped = false;
    const char* path = qtrlConfig.fontPath;
    FileSystem* fs = platform->getFileSystem();
    if (path[0] == '\0' || fs == nullptr || !fs->isSdCardInserted() || !fs->exists(path))
        return;

    // A file that fails half way may have replaced some of the UI fonts.
    fontSwapped = true;
    ownFont = display.loadCustomFontFromSD(fs, path);
    if (!ownFont)
        restoreUiFont();
}

void cQtrl::restoreUiFont()
{
    if (!fontSwapped)
        return;
    fontSwapped = false;
    ownFont = false;
    ThemeManager::applyFont(ThemeManager::getCurrentFontIndex());
}

void cQtrl::update()
{
    for (uint8_t n = 0; n < kQueueSize && s_queueTail != s_queueHead; n++)
    {
        uint8_t tail = s_queueTail;
        MidiEvent ev = s_queue[tail];
        compilerBarrier();
        s_queueTail = (tail + 1) & (kQueueSize - 1);
        handleIncoming(ev.status, ev.data1, ev.data2);
    }
    // Bounded so a long burst cannot starve the rest of the main loop.
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
        dirty |= 1 << qtrlScreenHeader;
    }
    if (logRequestPending && logRequestAge > kLogRequestDelayMs)
    {
        logRequestPending = false;
        requestSelectedLog();
    }

    if (dirty)
    {
        for (uint8_t part = 0; part < qtrlScreenPartCount; part++)
        {
            if (dirty & (1 << part))
                refreshPart(part);
        }
        dirty = 0;
        display.synchronizeRefresh();
    }
}

//==============================================================================
void cQtrl::sendCc(uint8_t cc, uint8_t value)
{
    if (cc > 127)
        return;
    platform->getUsbMidi()->sendControlChange(cc, value, qtrlConfig.controlChannel);
}

void cQtrl::sendSysEx(uint8_t command, uint16_t index, const char* text, uint16_t length)
{
    uint8_t message[8 + kChunk];
    if (length > kChunk)
        length = kChunk;
    memcpy(message, kSysExHeader, sizeof(kSysExHeader));
    message[4] = command;
    message[5] = (index >> 7) & 0x7F;
    message[6] = index & 0x7F;
    for (uint16_t i = 0; i < length; i++)
        message[7 + i] = (uint8_t)text[i] & 0x7F;
    message[7 + length] = 0xF7;
    platform->getUsbMidi()->sendSysEx(message, 8 + length);
}

void cQtrl::handleIncoming(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (status == 0xB0 && data1 == qtrlConfig.helloCc && data2 > 0)
    {
        // The server (re)started: answer so it sends its state again.
        sendCc(qtrlConfig.helloCc, 127);
    }
}

// F0 7D 51 54 <command> <index hi> <index lo> <ASCII text> F7 (see QtrlSysEx)
void cQtrl::handleSysEx(const uint8_t* data, uint16_t length)
{
    if (length < 8 || data[length - 1] != 0xF7)
        return;

    const uint8_t command = data[4];
    const uint16_t index = (uint16_t)((data[5] << 7) | data[6]);
    char text[kSysExLength];
    uint8_t n = 0;
    for (uint16_t i = 7; i + 1 < length && n + 1u < sizeof(text); i++)
    {
        uint8_t c = data[i];
        text[n++] = (c == '\t' || c == '\n' || (c >= 0x20 && c < 0x7F)) ? (char)c : '?';
    }
    text[n] = '\0';

    if (!D.connected)
    {
        D.connected = true;
        dirty |= 1 << qtrlScreenHeader;
    }

    char* f[4];
    switch (command)
    {
        case qtrlSysExServer:
            dirty |= 1 << qtrlScreenHeader;
            break;

        case qtrlSysExNotice:
            copyText(D.notice, text);
            noticeAge = 0;
            dirty |= 1 << qtrlScreenHeader;
            break;

        case qtrlSysExNode:
        {
            if (index >= QtrlConfig::kNodeCount)
                break;
            QtrlScreenData::Node& node = D.nodes[index];
            uint8_t count = splitTabs(text, f, 4);
            node.state = parseState(f[0][0]);
            if (count > 1 && f[1][0])
                copyText(node.name, f[1]);
            if (count > 2 && f[2][0])
                copyText(node.sub, f[2]);
            if (count > 3)
                copyText(node.status, f[3]);
            if (D.tab == qtrlTabStatus)
                dirty |= (1 << qtrlScreenLeft) | (1 << qtrlScreenRight);
            break;
        }

        case qtrlSysExSystem:
        {
            uint8_t count = splitTabs(text, f, 2);
            D.systemState = parseState(f[0][0]);
            copyText(D.systemStatus, count > 1 ? f[1] : "");
            if (D.tab == qtrlTabStatus)
                dirty |= 1 << qtrlScreenRight;
            break;
        }

        case qtrlSysExDetail:
            if (index == 0)
                D.detailCount = 0;
            if (index < QtrlScreenData::kMaxDetail)
            {
                for (uint8_t i = D.detailCount; i < index; i++)
                    D.detail[i][0] = '\0';
                if (text[0] != '\0')
                {
                    copyText(D.detail[index], text);
                    if (index + 1 > D.detailCount)
                        D.detailCount = index + 1;
                }
            }
            if (D.tab == qtrlTabStatus)
                dirty |= 1 << qtrlScreenRight;
            break;

        case qtrlSysExLogDate:
            if (index == 0)
                D.dateCount = 0;
            if (index < QtrlScreenData::kMaxDates && text[0] != '\0')
            {
                copyText(D.dates[index], text);
                if (index + 1 > D.dateCount)
                    D.dateCount = index + 1;
            }
            if (D.selectedDate >= D.dateCount)
                D.selectedDate = D.dateCount - 1;
            if (D.tab == qtrlTabLogs)
                dirty |= 1 << qtrlScreenLeft;
            break;

        case qtrlSysExLogBegin:
            if (index < D.dateCount)
            {
                D.selectedDate = index;
                uint8_t row = index / QtrlScreenData::kDateColumns;
                if (row < D.dateTopRow)
                    D.dateTopRow = row;
                else if (row >= D.dateTopRow + QtrlScreenData::kDateRows)
                    D.dateTopRow = row - QtrlScreenData::kDateRows + 1;
            }
            copyText(D.logDate, text);
            D.logLoading = true;
            D.logCount = 0;
            D.logTop = 0;
            followLog = true;
            if (D.tab == qtrlTabLogs)
                dirty |= (1 << qtrlScreenLeft) | (1 << qtrlScreenRight);
            break;

        case qtrlSysExLogLine:
        {
            if (index >= QtrlScreenData::kMaxLogLines || index > D.logCount)
                break;
            uint8_t count = splitTabs(text, f, 2);
            copyText(D.logTime[index], f[0]);
            copyText(D.logText[index], count > 1 ? f[1] : "");
            if (index == D.logCount)
                D.logCount++;
            if (!D.logLoading)
            {
                // a new line of the day being shown
                if (followLog && D.logCount > QtrlScreenData::kLogRows)
                    D.logTop = D.logCount - QtrlScreenData::kLogRows;
                if (D.tab == qtrlTabLogs)
                    dirty |= 1 << qtrlScreenRight;
            }
            break;
        }

        case qtrlSysExLogEnd:
            D.logLoading = false;
            if (index < D.logCount)
                D.logCount = index;
            D.logTop = D.logCount > QtrlScreenData::kLogRows ? D.logCount - QtrlScreenData::kLogRows : 0;
            if (D.tab == qtrlTabLogs)
                dirty |= 1 << qtrlScreenRight;
            break;

        case qtrlSysExChatReset:
            qtrlScreenChatClear(D);
            D.chatBusy = false;
            if (D.tab == qtrlTabChat)
                dirty |= 1 << qtrlScreenRight;
            break;

        case qtrlSysExChatBegin:
            copyText(chatSpeaker, text);
            chatPendingLength = 0;
            break;

        case qtrlSysExChatText:
            for (uint8_t i = 0; i < n && chatPendingLength < kChatPendingLength; i++)
                s_chatPending[chatPendingLength++] = text[i];
            break;

        case qtrlSysExChatEnd:
            s_chatPending[chatPendingLength] = '\0';
            qtrlScreenChatAdd(D, chatSpeaker, s_chatPending);
            chatPendingLength = 0;
            if (D.tab == qtrlTabChat)
                dirty |= 1 << qtrlScreenRight;
            break;

        case qtrlSysExChatBusy:
            D.chatBusy = index != 0;
            if (D.tab == qtrlTabChat)
                dirty |= 1 << qtrlScreenRight;
            break;

        default: break;
    }
}

//==============================================================================
void cQtrl::onButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress && state != buttonRelease)
        return;
    const QtrlConfig& cfg = qtrlConfig;
    const bool press = (state == buttonPress);

    // Shift changes the keyboard to its second layer while held.
    if (button == interfaceButtonShift)
    {
        if (D.shift != press)
        {
            D.shift = press;
            if (D.tab == qtrlTabChat)
                dirty |= 1 << qtrlScreenLeft;
        }
        return;
    }
    if (!press)
        return;

    const bool shift = shiftHeld();

    // Shift + exit button always leaves the screen, whatever the JSON says.
    if (shift && button == cfg.exitButton)
    {
        exitScreen(false);
        return;
    }

    if (D.tab == qtrlTabQuit && button == interfaceButton6)
    {
        setTab(qtrlTabHome); // Cancel
        return;
    }

    if (D.tab == qtrlTabChat && button == cfg.sendButton)
    {
        if (shift)
        {
            if (D.inputLength < QtrlScreenData::kInputLength)
            {
                D.input[D.inputLength++] = '\n';
                D.input[D.inputLength] = '\0';
                dirty |= 1 << qtrlScreenLeft;
            }
        }
        else
        {
            sendChat();
        }
        return;
    }

    if (D.tab == qtrlTabLogs && !shift)
    {
        switch (button)
        {
            case interfaceButtonLeft: moveDate(-1, 0); return;
            case interfaceButtonRight: moveDate(1, 0); return;
            case interfaceButtonUp: moveDate(0, -1); return;
            case interfaceButtonDown: moveDate(0, 1); return;
            default: break;
        }
    }

    runAction(qtrlConfigAction(cfg, button, shift));
}

void cQtrl::runAction(uint8_t action)
{
    switch (action)
    {
        case qtrlActionHome: setTab(qtrlTabHome); break;
        case qtrlActionStatus: setTab(qtrlTabStatus); break;
        case qtrlActionLogs: setTab(qtrlTabLogs); break;
        case qtrlActionChat: setTab(qtrlTabChat); break;
        case qtrlActionHelp: setTab(qtrlTabHelp); break;
        case qtrlActionQuit:
            if (D.tab == qtrlTabQuit)
                exitScreen(false);
            else
                setTab(qtrlTabQuit);
            break;
        case qtrlActionUndo: sendSysEx(qtrlSysExUndo, 0, "", 0); break;
        case qtrlActionRedo: sendSysEx(qtrlSysExRedo, 0, "", 0); break;
        case qtrlActionConfig: exitScreen(true); break;
        case qtrlActionTabPrev:
        case qtrlActionTabNext:
        {
            // Home .. Help (QUIT only by its own button)
            const uint8_t count = qtrlTabQuit;
            uint8_t tab = D.tab >= count ? 0 : D.tab;
            tab = (action == qtrlActionTabNext) ? (tab + 1) % count : (tab + count - 1) % count;
            setTab(tab);
            break;
        }
        default: break;
    }
}

void cQtrl::setTab(uint8_t tab)
{
    if (tab >= qtrlTabCount)
        return;
    D.tab = tab;
    D.pressedKey = -1;
    if (tab == qtrlTabLogs && D.selectedDate < 0 && D.dateCount > 0)
    {
        D.selectedDate = D.dateCount - 1;
        requestSelectedLog();
    }
    showPadLeds();
    refreshAll();
    display.synchronizeRefresh();
}

void cQtrl::moveDate(int8_t columns, int8_t rows)
{
    if (D.dateCount == 0)
        return;
    int16_t index = D.selectedDate < 0 ? D.dateCount - 1 : D.selectedDate;
    index += columns + rows * QtrlScreenData::kDateColumns;
    if (index < 0 || index >= D.dateCount)
        return;
    D.selectedDate = index;
    uint8_t row = index / QtrlScreenData::kDateColumns;
    if (row < D.dateTopRow)
        D.dateTopRow = row;
    else if (row >= D.dateTopRow + QtrlScreenData::kDateRows)
        D.dateTopRow = row - QtrlScreenData::kDateRows + 1;
    logRequestPending = true;
    logRequestAge = 0;
    dirty |= 1 << qtrlScreenLeft;
}

void cQtrl::requestSelectedLog()
{
    if (D.selectedDate < 0 || D.selectedDate >= D.dateCount)
        return;
    const char* date = D.dates[D.selectedDate];
    copyText(D.logDate, date);
    D.logLoading = true;
    D.logCount = 0;
    D.logTop = 0;
    sendSysEx(qtrlSysExLogRequest, (uint16_t)D.selectedDate, date, (uint16_t)strlen(date));
    dirty |= 1 << qtrlScreenRight;
}

//==============================================================================
void cQtrl::onPad(uint8_t pad, uint8_t state)
{
    if (pad >= kPadCount || (state != 0 && state != 1) || D.tab != qtrlTabChat)
        return;
    if (state == 1)
    {
        D.pressedKey = (int8_t)pad;
        typeKey(pad);
        padsBacklight.setFrontLayer(1, qtrlConfig.ledPressed, pad);
    }
    else
    {
        if (D.pressedKey == (int8_t)pad)
            D.pressedKey = -1;
        showPadLeds();
    }
    dirty |= 1 << qtrlScreenLeft;
}

void cQtrl::typeKey(uint8_t pad)
{
    D.shift = shiftHeld();
    const char* key = qtrlScreenKey(D, pad);
    if (strcmp(key, "BS") == 0)
    {
        if (D.inputLength > 0)
            D.input[--D.inputLength] = '\0';
        return;
    }
    if (strcmp(key, "CAPS") == 0)
    {
        D.caps = !D.caps;
        return;
    }
    const char* add = strcmp(key, "SP") == 0 ? " " : key;
    for (const char* p = add; *p && D.inputLength < QtrlScreenData::kInputLength; p++)
    {
        char c = *p;
        if (D.caps && !D.shift && c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if ((uint8_t)c < 0x20 || (uint8_t)c >= 0x7F)
            continue; // the server and the screen take ASCII only
        D.input[D.inputLength++] = c;
    }
    D.input[D.inputLength] = '\0';
}

void cQtrl::sendChat()
{
    if (D.inputLength == 0)
        return;
    if (!D.connected)
    {
        copyText(D.notice, qtrlConfig.waiting);
        noticeAge = 0;
        dirty |= 1 << qtrlScreenHeader;
        return;
    }
    uint16_t offset = 0;
    uint16_t part = 0;
    while (D.inputLength - offset > kChunk)
    {
        sendSysEx(qtrlSysExChatPart, part++, D.input + offset, kChunk);
        offset += kChunk;
    }
    sendSysEx(qtrlSysExChatSend, part, D.input + offset, D.inputLength - offset);
    D.inputLength = 0;
    D.input[0] = '\0';
    dirty |= 1 << qtrlScreenLeft;
}

void cQtrl::onJog(int16_t value)
{
    if (value == 0)
        return;
    if (D.tab == qtrlTabLogs)
    {
        int32_t top = (int32_t)D.logTop + value;
        int32_t maxTop = D.logCount > QtrlScreenData::kLogRows ? D.logCount - QtrlScreenData::kLogRows : 0;
        if (top < 0)
            top = 0;
        if (top > maxTop)
            top = maxTop;
        D.logTop = (uint16_t)top;
        followLog = (D.logTop == maxTop);
        dirty |= 1 << qtrlScreenRight;
    }
    else if (D.tab == qtrlTabChat)
    {
        // right = towards the newest line
        int32_t scroll = (int32_t)D.chatScroll - value;
        uint16_t total = D.chatCount + (D.chatBusy ? 1 : 0);
        int32_t maxScroll = total > QtrlScreenData::kChatRows ? total - QtrlScreenData::kChatRows : 0;
        if (scroll < 0)
            scroll = 0;
        if (scroll > maxScroll)
            scroll = maxScroll;
        D.chatScroll = (uint16_t)scroll;
        dirty |= 1 << qtrlScreenRight;
    }
}

// Chat tab: the keyboard on the pads (dim keys, brighter home position).
void cQtrl::showPadLeds()
{
    const QtrlConfig& cfg = qtrlConfig;
    for (uint8_t pad = 0; pad < kPadCount; pad++)
    {
        uint8_t level = 0;
        if (D.tab == qtrlTabChat)
        {
            const char* key = cfg.keys[pad / QtrlConfig::kKeyColumns][pad % QtrlConfig::kKeyColumns];
            if (key[0] != '\0')
                level = cfg.ledKey;
            for (uint8_t i = 0; i < cfg.markedCount; i++)
            {
                if (cfg.markedKeys[i] == pad)
                    level = cfg.ledMarked;
            }
            if (D.caps && strcmp(key, "CAPS") == 0)
                level = cfg.ledMarked;
            if (D.pressedKey == (int8_t)pad)
                level = cfg.ledPressed;
        }
        if (level > 0)
        {
            padsBacklight.setFrontLayer(1, level, pad);
            padLit[pad] = true;
        }
        else if (padLit[pad])
        {
            padsBacklight.setFrontLayer(0, 0, pad);
            padLit[pad] = false;
        }
    }
}

void cQtrl::exitScreen(bool openConfig)
{
    if (openConfig)
    {
        uint8_t button = interfaceButtonConfig;
        eventFunct(eventSwitchModule, this, &button, 0);
        return;
    }
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

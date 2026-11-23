#ifndef INTERFACE_QTRL_QTRLCONFIG_H_
#define INTERFACE_QTRL_QTRLCONFIG_H_

#include <stddef.h>
#include <stdint.h>

// Settings of the Q.T.R.L. screen (Quick Terminal for Remote & Logs).
// Defaults are compiled in and can be overridden by /QtrlControl.json on the
// SD card (sample: sd-card/QtrlControl.json).
//
// Protocol with the server (qtrl-server on the Mac), all on controlChannel:
//   both ways       : CC helloCc, 127 = screen opened / server (re)started,
//                     0 = screen closed. On 127 the server sends everything.
//   texts, requests : SysEx F0 7D 51 54 <command> <index hi> <index lo> <ASCII> F7
//                     (see QtrlSysEx). 7D = non-commercial manufacturer id,
//                     "QT" (51 54) marks the messages as Q.T.R.L.'s.

enum QtrlSysEx : uint8_t
{
    // server -> Tracker
    qtrlSysExServer = 0x01,     // text: server name (marks the link as up)
    qtrlSysExNotice = 0x02,     // text: short message for the title bar
    qtrlSysExNode = 0x10,       // index: node slot, text: "state<TAB>name<TAB>sub<TAB>status"
    qtrlSysExSystem = 0x11,     // text: "state<TAB>status" of the whole system
    qtrlSysExDetail = 0x12,     // index: line (0 starts a new list), text: detail line
    qtrlSysExLogDate = 0x20,    // index: position (0 starts a new list), text: "YYYY.MM.DD"
    qtrlSysExLogBegin = 0x21,   // index: date position, text: date
    qtrlSysExLogLine = 0x22,    // index: line, text: "time<TAB>message"
    qtrlSysExLogEnd = 0x23,     // index: line count
    qtrlSysExChatReset = 0x30,  // clears the conversation
    qtrlSysExChatBegin = 0x31,  // text: speaker
    qtrlSysExChatText = 0x32,   // text: part of the message ('\n' = new line)
    qtrlSysExChatEnd = 0x33,    // the message is complete
    qtrlSysExChatBusy = 0x34,   // index: 1 = the assistant is answering, 0 = idle

    // Tracker -> server
    qtrlSysExLogRequest = 0x40, // index: date position, text: date
    qtrlSysExChatPart = 0x41,   // text: part of the message being sent
    qtrlSysExChatSend = 0x42,   // text: last part; the server answers
    qtrlSysExUndo = 0x43,
    qtrlSysExRedo = 0x44,
};

// Node states in qtrlSysExNode / qtrlSysExSystem
enum QtrlNodeState : uint8_t
{
    qtrlStateUnknown = 0, // 'U': no report yet
    qtrlStateOk,          // 'O'
    qtrlStateAnomaly,     // 'A'
};

enum QtrlTab : uint8_t
{
    qtrlTabHome = 0,
    qtrlTabStatus,
    qtrlTabLogs,
    qtrlTabChat,
    qtrlTabHelp,
    qtrlTabQuit,
    qtrlTabCount
};

// What a button can do ("bindings" and "screenButtons" in the JSON)
enum QtrlAction : uint8_t
{
    qtrlActionNone = 0,
    qtrlActionHome,
    qtrlActionStatus,
    qtrlActionLogs,
    qtrlActionChat,
    qtrlActionHelp,
    qtrlActionQuit, // opens the QUIT tab; on the QUIT tab it leaves the screen
    qtrlActionUndo,
    qtrlActionRedo,
    qtrlActionConfig, // leaves the screen and opens the Tracker's Config
    qtrlActionTabPrev,
    qtrlActionTabNext,
    qtrlActionCount
};

struct QtrlConfig
{
    static constexpr uint8_t kNodeCount = 5; // Terminal, QTRL1, QTRL2, FAB1, Server
    static constexpr uint8_t kListRows = 12;
    static constexpr uint8_t kListKeyLength = 20;
    static constexpr uint8_t kListValueLength = 40;
    static constexpr uint8_t kBindings = 16;
    static constexpr uint8_t kKeyRows = 4;
    static constexpr uint8_t kKeyColumns = 12;
    static constexpr uint8_t kKeyLength = 6; // "BS", "CAPS", "SP" or one character
    static constexpr uint8_t kMarkedKeys = 4;

    uint8_t controlChannel; // 1..16
    uint8_t helloCc;

    // A press of button `button` (with Shift when `shift`) runs `action`.
    struct Binding
    {
        uint8_t button;
        bool shift;
        uint8_t action;
    };
    Binding bindings[kBindings];
    uint8_t bindingCount;
    uint8_t screenButtons[8]; // action of the buttons under the screen
    uint8_t sendButton;       // chat: send (with Shift: new line)
    uint8_t exitButton;       // Shift + this leaves the screen at once

    // Texts. UTF-8; ASCII and the copyright sign are drawn, others show as '?'.
    char fontPath[48];
    char title[16];
    char tagline[40];
    char credit[24];
    char headings[qtrlTabCount][12]; // "> Home" etc. (title bar and pane)
    char labels[8][12];
    char welcome[40];
    char intro[96];
    char homeKeys[kListRows][kListKeyLength];
    char homeValues[kListRows][kListValueLength];
    char helpKeys[kListRows][kListKeyLength];
    char helpValues[kListRows][kListValueLength];
    char systemLabel[20];
    char checkDetail[32];
    char waiting[32];
    char nodeNames[kNodeCount][12];
    char nodeSubs[kNodeCount][24];
    char statusUnknown[24];
    char logsHint[40];
    char logHint[40];
    char logEmpty[32];
    char logLoading[24];
    char chatHint[80];
    char chatBusy[32];
    char quitBefore[40];
    char quitWord[12];
    char quitAfter[24];
    char cancel[12];
    char cancelSub[20];

    // Chat keyboard on the pads: 4 rows x 12 keys, top row first.
    char keys[kKeyRows][kKeyColumns][kKeyLength];
    char shiftKeys[kKeyRows][kKeyColumns][kKeyLength];
    uint8_t markedKeys[kMarkedKeys]; // pads drawn filled (home position)
    uint8_t markedCount;
    uint8_t ledKey;    // 0..31
    uint8_t ledMarked;
    uint8_t ledPressed;

    uint32_t backgroundColor; // 0xRRGGBB
    uint32_t textColor;
    uint32_t subTextColor;
    uint32_t lineColor;
    uint32_t okColor;
    uint32_t alertColor;
    uint32_t unknownColor;
    uint32_t selectColor;
    uint32_t quitColor;

    bool loadedFromSd;
};

extern QtrlConfig qtrlConfig;

void qtrlConfigSetDefaults(QtrlConfig& cfg);

// Parses JSON text into cfg. Unknown keys are ignored; keys that are present
// override the current values. Returns false on a syntax error.
bool qtrlConfigParse(const char* json, size_t len, QtrlConfig& cfg);

// Resets to defaults, then applies /QtrlControl.json when it exists.
void qtrlConfigLoad();

// Action for a button press, or qtrlActionNone.
uint8_t qtrlConfigAction(const QtrlConfig& cfg, uint8_t button, bool shift);

#endif /* INTERFACE_QTRL_QTRLCONFIG_H_ */

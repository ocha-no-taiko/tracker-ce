#ifndef INTERFACE_QTRL_QTRLSCREEN_H_
#define INTERFACE_QTRL_QTRLSCREEN_H_

#include "displayControls.h"
#include "qtrl/qtrlConfig.h"

// The Q.T.R.L. screen: one display control per part, so new chat text only
// rebuilds the right pane. Layout follows the design (QTRL_UI_v1) scaled to
// the 800 x 480 screen; texts and colors come from QtrlControl.json, node
// states, logs and chat from the server.

enum QtrlScreenPart : uint8_t
{
    qtrlScreenBackground = 0, // fill, frame, title, title line, divider
    qtrlScreenHeader,         // "> Tab" and the notice in the title bar
    qtrlScreenLeft,           // left pane of the tab
    qtrlScreenRight,          // right pane of the tab
    qtrlScreenLabels,         // screen-button labels at the bottom
    qtrlScreenPartCount
};

struct QtrlScreenData
{
    static constexpr uint8_t kNoticeLength = 48;
    static constexpr uint8_t kMaxDetail = 6;
    static constexpr uint8_t kDetailLength = 64;
    static constexpr uint8_t kMaxDates = 120;
    static constexpr uint8_t kDateLength = 12;
    static constexpr uint16_t kMaxLogLines = 400;
    static constexpr uint8_t kLogTimeLength = 10;
    static constexpr uint8_t kLogTextLength = 72;
    static constexpr uint16_t kMaxChatLines = 300;
    static constexpr uint8_t kChatLineLength = 48;
    static constexpr uint16_t kInputLength = 240;
    static constexpr uint8_t kDateColumns = 3;
    static constexpr uint8_t kDateRows = 3;   // visible rows of the date grid
    static constexpr uint8_t kLogRows = 12;   // visible log lines
    static constexpr uint8_t kChatRows = 15;  // visible chat lines

    const QtrlConfig* cfg;
    bool ownFont; // Qtrl.ptfont is loaded (otherwise the UI font is used)

    uint8_t tab;
    bool connected;
    char notice[kNoticeLength];

    struct Node
    {
        uint8_t state; // QtrlNodeState
        char name[12];
        char sub[24];
        char status[32];
    };
    Node nodes[QtrlConfig::kNodeCount];
    uint8_t systemState;
    char systemStatus[32];
    uint8_t detailCount;
    char detail[kMaxDetail][kDetailLength];

    // Logs: dates (oldest first) and the lines of the selected one
    uint8_t dateCount;
    char dates[kMaxDates][kDateLength];
    int16_t selectedDate; // -1 = none
    uint8_t dateTopRow;   // first visible row of the grid
    char logDate[kDateLength];
    bool logLoading;
    uint16_t logCount;
    uint16_t logTop; // first visible line
    char logTime[kMaxLogLines][kLogTimeLength];
    char logText[kMaxLogLines][kLogTextLength];

    // Chat: wrapped lines in a ring (oldest dropped first)
    uint16_t chatStart;
    uint16_t chatCount;
    uint16_t chatScroll; // lines scrolled up from the newest
    bool chatBusy;
    char chat[kMaxChatLines][kChatLineLength];
    char input[kInputLength + 1];
    uint16_t inputLength;
    bool caps;
    bool shift;
    int8_t pressedKey; // pad being held, -1 = none

    char labels[8][12];
};

// Clears everything the server sends (nodes, logs, chat).
void qtrlScreenReset(QtrlScreenData& data);

// Chat: adds a message ("speaker:" line, the text wrapped to the pane, a
// blank line). '\n' in text starts a new line.
void qtrlScreenChatAdd(QtrlScreenData& data, const char* speaker, const char* text);
void qtrlScreenChatClear(QtrlScreenData& data);
const char* qtrlScreenChatLine(const QtrlScreenData& data, uint16_t index);

// Text of the key on a pad (pads 0..47, top row first), with Shift held:
// from shiftKeys. Special keys are "BS", "CAPS" and "SP".
const char* qtrlScreenKey(const QtrlScreenData& data, uint8_t pad);

class cQtrlScreen : public cDisplayControl
{
  public:
    cQtrlScreen(strControlProperties* properties = nullptr);
    virtual ~cQtrlScreen() {}

    virtual void update();

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    // The part and the data are fixed when the control is made. A module that
    // was just left can still call setControlValue() / setControlData() with
    // its old handles (Config does after its menu opens this screen), and the
    // handle may by then belong to one of these parts.
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void*) {}

  private:
    const QtrlScreenData* data = nullptr;

    void drawBackground();
    void drawHeader();
    void drawLeft();
    void drawRight();
    void drawLabels();

    void drawHomeLeft();
    void drawHomeRight();
    void drawStatusLeft();
    void drawStatusRight();
    void drawLogsLeft();
    void drawLogsRight();
    void drawChatLeft();
    void drawChatRight();
    void drawHelpLeft();
    void drawHelpRight();
    void drawQuit();
};

#endif /* INTERFACE_QTRL_QTRLSCREEN_H_ */

#ifndef INTERFACE_Q4_Q4SCREEN_H_
#define INTERFACE_Q4_Q4SCREEN_H_

#include "displayControls.h"
#include "q4/q4Config.h"

// The Q-4 screen: one display control per part, so a turned value only
// rebuilds the right pane. Layout follows the design (Q-4_UI_v1, 640 x 360
// units) scaled to the 800 x 480 screen. Texts and colors come from
// Q4Control.json; pages, values and the waveform from Live.

enum Q4ScreenPart : uint8_t
{
    q4ScreenBackground = 0, // fill, frame, title, title line, divider
    q4ScreenHeader,         // "> Tab" and the notice in the title bar
    q4ScreenLeft,           // left pane (track info and panel / help tiles / QUIT)
    q4ScreenRight,          // right pane (parameter grid / help text)
    q4ScreenLabels,         // screen-button labels at the bottom
    q4ScreenPartCount
};

struct Q4ScreenData
{
    static constexpr uint8_t kCells = 12;   // 4 x 2 parameters + 4 actions
    static constexpr uint8_t kColumns = 4;
    static constexpr uint16_t kWaveMax = 172; // columns of the waveform
    static constexpr uint8_t kMaxSlices = 16;
    static constexpr uint16_t kNoMarker = 16383;

    const Q4Config* cfg;
    bool ownFont; // Q4.ptfont is loaded (otherwise the UI font is used)

    uint8_t tab; // Q4Tab shown
    bool connected;
    char notice[48];

    // The page Live sent last (only shown while it belongs to the tab)
    bool pageValid;
    uint8_t pageTab;
    uint8_t track;
    uint8_t page;
    uint8_t pageCount;
    uint8_t slot;
    uint8_t pageFlags; // bit0 bypassed, bit1 mapping mode
    uint8_t panel;     // Q4Panel
    char pageTitle[24];

    struct Cell
    {
        uint8_t kind; // Q4CellKind
        uint8_t flags; // Q4CellFlag
        uint8_t icon;  // Q4Icon
        uint16_t value; // 0..16383
        int16_t mod;    // -8192..8191: modulation depth of the current slot
        char label[16];
        char text[20];
        char mode[16];
    };
    Cell cells[kCells];
    uint8_t cursor;
    bool mapping; // MOD held

    // Status line of the left pane
    uint16_t scene;
    uint16_t bpmTenths;
    uint8_t statusFlags; // bit0 playing, bit1 recording
    char sampleName[48];

    // Waveform (Material)
    uint16_t waveCount;
    uint8_t waveMin[kWaveMax];
    uint8_t waveMax[kWaveMax];
    uint16_t markStart;
    uint16_t markEnd;
    uint16_t markLoop;
    uint16_t playhead;
    uint8_t sliceCount;
    uint16_t slices[kMaxSlices];

    // Help
    uint8_t helpPage;
    uint16_t helpScroll; // first visible line of the wrapped text

    char labels[8][12];
};

// Clears everything Live sends.
void q4ScreenReset(Q4ScreenData& data);

// Help text of the current page wrapped to the pane: number of lines, and
// how many fit on the screen.
uint16_t q4ScreenHelpLineCount(const Q4ScreenData& data);
uint8_t q4ScreenHelpVisibleLines();

class cQ4Screen : public cDisplayControl
{
  public:
    cQ4Screen(strControlProperties* properties = nullptr);
    virtual ~cQ4Screen() {}

    virtual void update();

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    // The part and the data are fixed when the control is made (a module that
    // was just left can still poke its old handles, see qtrlScreen.h).
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void*) {}

  private:
    const Q4ScreenData* data = nullptr;

    void drawBackground();
    void drawHeader();
    void drawLeft();
    void drawRight();
    void drawLabels();

    void drawDeviceLeft();
    void drawDeviceRight();
    void drawCell(uint8_t index, int16_t x, int16_t y, int16_t w, int16_t h);
    void drawWave();
    void drawColorPanel();
    void drawSpacePanel();
    void drawHelpLeft();
    void drawHelpRight();
    void drawCredit();
    void drawQuit();
};

#endif /* INTERFACE_Q4_Q4SCREEN_H_ */

#ifndef INTERFACE_SOTI_SOTISCREEN_H_
#define INTERFACE_SOTI_SOTISCREEN_H_

#include "displayControls.h"
#include "soti/sotiConfig.h"

// The Synthesis of the Images screen (docs/mockup.png of the
// synthesis-of-the-images repository, 1928 x 1086, scaled to 800 x 480): files
// on the left, the 3 x 5 grid of values on the right, the eight actions below.
// One display control per part, so a turned value only rebuilds the right pane.

enum SotiScreenPart : uint8_t
{
    sotiScreenBackground = 0, // fill, frame, title, title line, divider, hints
    sotiScreenHeader,         // notice / slots and the selected value in the title bar
    sotiScreenLeft,           // file pane
    sotiScreenRight,          // control pane
    sotiScreenLabels,         // labels below the screen
    sotiScreenPartCount
};

struct SotiScreenData
{
    static constexpr uint16_t kMaxFiles = 256;
    static constexpr uint8_t kNameLength = 32;
    static constexpr uint8_t kColumns = 3;     // files per row, and slots A B C
    static constexpr uint8_t kFileRows = 3;    // rows of files on the screen
    static constexpr uint8_t kValues = 15;     // 5 parameters x 3 slots

    const SotiConfig* cfg;
    bool ownFont; // Soti.ptfont is loaded (otherwise the UI font is used)
    bool connected;

    uint8_t focus; // 0 files, 1 controls
    uint16_t fileCursor;
    uint16_t fileTop; // first row shown
    uint16_t fileCount;
    uint8_t fileFlags[kMaxFiles];
    char fileNames[kMaxFiles][kNameLength];

    uint16_t values[kValues]; // 0..16383
    uint8_t controlCursor;
    uint8_t select;      // SotiSelect
    uint8_t statusFlags; // SotiStatusFlag
    char slotNames[3][kNameLength];

    char notice[64];
    bool quitArmed;
};

// Clears everything the server sends.
void sotiScreenReset(SotiScreenData& data);

// True when the value at index is changed by the jog now (cursor + select mode).
bool sotiScreenIsSelected(const SotiScreenData& data, uint8_t index);

class cSotiScreen : public cDisplayControl
{
  public:
    cSotiScreen(strControlProperties* properties = nullptr);
    virtual ~cSotiScreen() {}

    virtual void update();

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    // The part and the data are fixed when the control is made (a module that
    // was just left can still poke its old handles, see qtrlScreen.h).
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void*) {}

  private:
    const SotiScreenData* data = nullptr;

    void drawBackground();
    void drawHeader();
    void drawFiles();
    void drawControls();
    void drawLabels();
    void drawFile(uint16_t index, int16_t x, int16_t y);
};

#endif /* INTERFACE_SOTI_SOTISCREEN_H_ */

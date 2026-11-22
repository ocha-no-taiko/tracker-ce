#ifndef INTERFACE_DAWCONTROL_DAWSCREEN_H_
#define INTERFACE_DAWCONTROL_DAWSCREEN_H_

#include "displayControls.h"
#include "dawControl/dawControlConfig.h"

// The DAW Control screen ("Live Tracker"): one display control per part, so
// a new file name or tempo only rebuilds the part it is in. The look follows
// the design in DAW_CONTROL.md; texts and colors come from DawControl.json,
// the help lines from the remote script in Live.

enum DawScreenPart : uint8_t
{
    dawScreenBackground = 0, // frame, title, logo, texts, divider, headings
    dawScreenInfo,           // values of the info rows
    dawScreenHelp,           // help rows
    dawScreenLabels,         // screen-button labels at the bottom
};

struct DawScreenData
{
    static constexpr uint8_t kMaxHelp = 16;
    static constexpr uint8_t kHelpKeyLength = 12;
    static constexpr uint8_t kHelpValueLength = 40;
    static constexpr uint8_t kInfoValueLength = 48;
    static constexpr uint8_t kLabelLength = 16;

    const DawControlConfig* cfg;
    bool dawFont; // the screen's own font is loaded (otherwise the UI font is used)

    char info[DawControlConfig::kInfoCount][kInfoValueLength];

    uint8_t helpCount;
    char helpKey[kMaxHelp][kHelpKeyLength];
    char helpValue[kMaxHelp][kHelpValueLength];

    char labels[8][kLabelLength];
};

// Fills the help rows with the built-in list (the default mapping of the
// remote script), shown until Live sends its own.
void dawScreenSetDefaultHelp(DawScreenData& data);

// Stores one help line from the DAW: "key<TAB>value", or "" for a blank row.
void dawScreenSetHelpLine(DawScreenData& data, uint8_t index, const char* text);

class cDawScreen : public cDisplayControl
{
  public:
    cDawScreen(strControlProperties* properties = nullptr);
    virtual ~cDawScreen() {}

    virtual void update();

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { this->data = (const DawScreenData*)data; }

  private:
    const DawScreenData* data = nullptr;

    void drawBackground();
    void drawInfo();
    void drawHelp();
    void drawLabels();
};

#endif /* INTERFACE_DAWCONTROL_DAWSCREEN_H_ */

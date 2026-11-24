#ifndef INTERFACE_Q4_Q4_H_
#define INTERFACE_Q4_Q4_H_

#include "core/modulesBase.h"
#include "elapsedMillis.h"
#include "q4/q4Config.h"

// Q-4 screen: the Tracker as the panel of a sound-sculpting sampler built in
// Ableton Live (Q4 remote script). Tabs Material .. MOD show the pages Live
// sends; Help and QUIT are drawn here. The Tracker's own sequencer is stopped
// while it is open.

// Called through the DAW Control receive hooks (dawControl.cpp), which the
// USB MIDI handlers use for all control screens.
bool q4IsActive();
void q4ReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2);
void q4ReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete);

class cQ4 : public ModuleBase
{
  public:
    static constexpr uint8_t kPadCount = 48;

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cQ4() {}
    ~cQ4() {}

    void onButton(uint8_t button, uint8_t state);
    void onPad(uint8_t pad, uint8_t state);
    void onJog(int16_t value);

    // display (gQ4.cpp)
    void showScreen();
    void refreshPart(uint8_t part);
    void refreshAll();

  private:
    void sendCc(uint8_t cc, uint8_t value);
    void sendDelta(uint8_t cc, int16_t steps);
    void handleIncoming(uint8_t status, uint8_t data1, uint8_t data2);
    void handleSysEx(const uint8_t* data, uint16_t length);
    void loadScreenFont();
    void restoreUiFont();

    void setTab(uint8_t tab);
    void pressTab(uint8_t tab, bool shift);
    void moveCursor(int8_t columns, int8_t rows);
    void sendCursor();
    void fixCursor();
    void moveHelp(int8_t columns, int8_t rows);
    void pressCell(bool press);
    void allNotesOff();
    void showPadLeds();
    void exitScreen();

    bool fontSwapped = false;
    bool ownFont = false;
    elapsedMillis noticeAge;
    uint8_t dirty = 0; // bit per Q4ScreenPart
    bool pageChanged = false;
    uint8_t lastDeviceTab = 0;

    // MOD held: mapping mode; a press with nothing done meanwhile is a tap
    bool modHeld = false;
    bool modUsed = false;

    int8_t pressedCell = -1;   // action cell held with the press button
    int8_t trigChannel = -1;   // channel of a TRIG note being held (-1 = none)
    uint8_t padNote[kPadCount] = {};
    uint8_t padChannel[kPadCount] = {};
    bool padDown[kPadCount] = {};
    uint8_t padsHeld = 0;
    bool padLit[kPadCount] = {};

  public:
    hControl screenPart[5] = { -1, -1, -1, -1, -1 };
};

extern cQ4 q4;

#endif /* INTERFACE_Q4_Q4_H_ */

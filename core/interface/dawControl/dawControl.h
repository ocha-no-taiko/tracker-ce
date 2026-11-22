#ifndef INTERFACE_DAWCONTROL_DAWCONTROL_H_
#define INTERFACE_DAWCONTROL_DAWCONTROL_H_

#include "core/modulesBase.h"
#include "dawControl/dawControlConfig.h"

// DAW Control screen: turns the Tracker into a USB MIDI control surface.
// Buttons, pads and the jog wheel are sent to the DAW (Ableton Live with the
// PolyendTrackerDAW remote script), and pad LEDs / the screen show what the
// DAW sends back. The Tracker's own sequencer is stopped while it is open.

// Called from the USB MIDI receive handlers (mtMidi.cpp). While the screen is
// open every USB note/CC goes through here instead of the normal handlers.
bool dawControlIsActive();
void dawControlReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2);

class cDawControl : public ModuleBase
{
  public:
    static constexpr uint8_t kPadCount = 48;
    static constexpr uint8_t kPadColumns = 12;
    static constexpr uint8_t kPadRows = 4;
    static constexpr uint8_t kNoNote = 255;

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cDawControl() {}
    ~cDawControl() {}

    // input
    void onButton(uint8_t button, uint8_t state);
    void onPad(uint8_t pad, uint8_t state);
    void onJog(int16_t value);

    // display (gDawControl.cpp)
    void showDefaultScreen();
    void refreshStates();
    void refreshInfo();
    void refreshBottomLabels();

    // state from the DAW
    uint8_t states[dawStateCount] = {};
    uint16_t tempoTenths = 1200;
    uint8_t tempoMsb = 0;
    uint8_t ringTrack = 0;
    uint8_t ringScene = 0;
    uint8_t selectedTrack = 0;
    bool connected = false;

    // local state
    bool sessionMode = true;
    int8_t octaveShift = 0;

  private:
    void sendCc(uint8_t cc, uint8_t value);
    void sendNote(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleIncoming(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2);

    void setPadLight(uint8_t pad, uint8_t level, bool blink);
    void applyGridLed(uint8_t pad);
    void showSessionLeds();
    void showNoteLeds();
    uint8_t notePadLevel(uint8_t pad) const;
    int16_t padToNote(uint8_t pad) const;
    void releaseHeldNotes();
    void toggleMode();
    void exitScreen();

    uint8_t gridVelocity[kPadCount] = {};
    uint8_t heldNote[kPadCount] = {};
    bool blinking[kPadCount] = {};

    bool statesDirty = false;
    bool infoDirty = false;

  public:
    // display controls and their text buffers (texts are kept by pointer)
    hControl label[8];
    hControl bgLabel;
    hControl stateLabel[dawStateCount];
    hControl infoLabel[4];

    char titleText[32];
    char tempoText[24];
    char ringText[64];
    char modeText[48];
    char statusText[96];
    char modeButtonText[16];
};

extern cDawControl dawControl;

#endif /* INTERFACE_DAWCONTROL_DAWCONTROL_H_ */

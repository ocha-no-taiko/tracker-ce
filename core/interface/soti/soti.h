#ifndef INTERFACE_SOTI_SOTI_H_
#define INTERFACE_SOTI_SOTI_H_

#include "core/modulesBase.h"
#include "elapsedMillis.h"
#include "soti/sotiConfig.h"

// Synthesis of the Images screen: the Tracker as the panel of soti-server on
// the Mac (three images layered by depth, and their sounds). The Tracker's own
// sequencer is stopped while it is open.

// Called through the DAW Control receive hooks (dawControl.cpp), which the
// USB MIDI handlers use for all control screens.
bool sotiIsActive();
void sotiReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2);
void sotiReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete);

class cSoti : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cSoti() {}
    ~cSoti() {}

    void onButton(uint8_t button, uint8_t state);
    void onJog(int16_t value);

    // display (gSoti.cpp)
    void showScreen();
    void refreshPart(uint8_t part);
    void refreshAll();

  private:
    void sendCc(uint8_t cc, uint8_t value);
    void sendDelta(uint8_t cc, int16_t steps);
    void sendFileCursor();
    void sendHello();
    void handleIncoming(uint8_t status, uint8_t data1, uint8_t data2);
    void handleSysEx(const uint8_t* data, uint16_t length);
    void loadScreenFont();
    void restoreUiFont();

    void setFocus(uint8_t focus);
    void moveFile(int16_t columns, int16_t rows);
    void setFileCursor(int32_t index);
    void moveControl(int8_t columns, int8_t rows);
    void pressAction(uint8_t action);
    void exitScreen();

    bool fontSwapped = false;
    bool ownFont = false;
    elapsedMillis noticeAge;
    elapsedMillis quitAge;
    uint8_t dirty = 0; // bit per SotiScreenPart

  public:
    hControl screenPart[5] = { -1, -1, -1, -1, -1 };
};

extern cSoti soti;

#endif /* INTERFACE_SOTI_SOTI_H_ */

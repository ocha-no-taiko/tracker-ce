#ifndef INTERFACE_QTRL_QTRL_H_
#define INTERFACE_QTRL_QTRL_H_

#include "core/modulesBase.h"
#include "elapsedMillis.h"
#include "qtrl/qtrlConfig.h"

// Q.T.R.L. (Quick Terminal for Remote & Logs) screen: the Tracker as the
// terminal of the qtrl-server running on the Mac. Tabs (Home, Status, Logs,
// Chat, Help, QUIT) are drawn here; node states, logs and chat answers come
// from the server over USB MIDI (see qtrlConfig.h for the protocol). The
// Tracker's own sequencer is stopped while it is open.

// Called through the DAW Control receive hooks (dawControl.cpp), which the
// USB MIDI handlers use for both control screens.
bool qtrlIsActive();
void qtrlReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2);
void qtrlReceiveUsbSysEx(const uint8_t* data, uint16_t length, bool complete);

class cQtrl : public ModuleBase
{
  public:
    static constexpr uint8_t kPadCount = 48;

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cQtrl() {}
    ~cQtrl() {}

    void onButton(uint8_t button, uint8_t state);
    void onPad(uint8_t pad, uint8_t state);
    void onJog(int16_t value);

    // display (gQtrl.cpp)
    void showScreen();
    void refreshPart(uint8_t part);
    void refreshAll();

  private:
    void sendCc(uint8_t cc, uint8_t value);
    void sendSysEx(uint8_t command, uint16_t index, const char* text, uint16_t length);
    void handleIncoming(uint8_t status, uint8_t data1, uint8_t data2);
    void handleSysEx(const uint8_t* data, uint16_t length);
    void loadScreenFont();
    void restoreUiFont();

    void runAction(uint8_t action);
    void setTab(uint8_t tab);
    void moveDate(int8_t columns, int8_t rows);
    void requestSelectedLog();
    void typeKey(uint8_t pad);
    void sendChat();
    void showPadLeds();
    void exitScreen(bool openConfig);

    bool fontSwapped = false;
    bool ownFont = false;
    elapsedMillis noticeAge;
    elapsedMillis logRequestAge;
    bool logRequestPending = false;
    bool followLog = true; // keep the newest log line in view
    uint8_t dirty = 0;     // bit per QtrlScreenPart
    bool padLit[kPadCount] = {};

    char chatSpeaker[16] = {};
    uint16_t chatPendingLength = 0;

  public:
    hControl screenPart[5] = { -1, -1, -1, -1, -1 };
};

extern cQtrl qtrl;

#endif /* INTERFACE_QTRL_QTRL_H_ */

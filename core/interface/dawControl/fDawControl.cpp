#include "dawControl/dawControl.h"

#include <string.h>

#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "core/songTimer.h"

cDawControl dawControl;
static cDawControl* DC = &dawControl;

//==============================================================================
// Incoming USB MIDI queue. The receive handlers may run from the audio
// callback, so they only enqueue; update() does the work in the main loop.
namespace
{
struct MidiEvent
{
    uint8_t status;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
};

constexpr uint8_t kQueueSize = 128; // power of two
MidiEvent s_queue[kQueueSize];
volatile uint8_t s_queueHead = 0;
volatile uint8_t s_queueTail = 0;
volatile bool s_active = false;

inline void compilerBarrier()
{
    __asm volatile("" ::: "memory");
}
} // namespace

bool dawControlIsActive()
{
    return s_active;
}

void dawControlReceiveUsbMidi(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2)
{
    if (channel != dawControlConfig.controlChannel)
        return;

    uint8_t head = s_queueHead;
    uint8_t next = (head + 1) & (kQueueSize - 1);
    if (next == s_queueTail)
        return; // full: drop

    s_queue[head] = { status, channel, data1, data2 };
    compilerBarrier();
    s_queueHead = next;
}

//==============================================================================
static void functButton(uint8_t button, uint8_t state)
{
    DC->onButton(button, state);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    DC->onPad(pad, state);
}

static void functJog(int16_t value)
{
    DC->onJog(value);
}

//==============================================================================
void cDawControl::start([[maybe_unused]] uint32_t options)
{
    dawControlConfigLoad();

    if (!sequencer.isStop())
    {
        sequencer.stop();
    }

    memset(states, 0, sizeof(states));
    memset(gridVelocity, 0, sizeof(gridVelocity));
    memset(heldNote, kNoNote, sizeof(heldNote));
    memset(blinking, 0, sizeof(blinking));
    tempoTenths = 1200;
    tempoMsb = 0;
    ringTrack = 0;
    ringScene = 0;
    selectedTrack = 0;
    connected = false;
    sessionMode = true;
    octaveShift = 0;

    songTimer.hide();

    for (uint8_t i = 0; i < interfaceButtonsCount; i++)
    {
        ip->setButtonObj(i, functButton);
    }
    ip->setPotObj(interfacePot0, functJog, false);
    ip->setPadsGlobal(functPads);

    s_queueTail = s_queueHead;
    s_active = true;

    padsBacklight.clearGlobalPads(1, 1, 0);
    showDefaultScreen();
    showSessionLeds();

    // Tells the remote script to send its full state.
    sendCc(dawControlConfig.helloCc, 127);

    moduleRefresh = 1;
}

void cDawControl::stop()
{
    releaseHeldNotes();
    sendCc(dawControlConfig.helloCc, 0);

    s_active = false;

    for (uint8_t i = 0; i < kPadCount; i++)
    {
        if (blinking[i])
        {
            padsBacklight.stopBlink(i);
            blinking[i] = false;
        }
    }
    padsBacklight.clearGlobalPads(1, 1, 0);

    songTimer.show();
    moduleRefresh = 0;
}

void cDawControl::update()
{
    // Bounded so a flood of messages cannot starve the rest of the main loop.
    for (uint8_t n = 0; n < kQueueSize && s_queueTail != s_queueHead; n++)
    {
        uint8_t tail = s_queueTail;
        MidiEvent ev = s_queue[tail];
        compilerBarrier();
        s_queueTail = (tail + 1) & (kQueueSize - 1);

        handleIncoming(ev.status, ev.channel, ev.data1, ev.data2);
    }

    bool refreshed = false;
    if (statesDirty)
    {
        statesDirty = false;
        refreshStates();
        refreshed = true;
    }
    if (infoDirty)
    {
        infoDirty = false;
        refreshInfo();
        refreshed = true;
    }
    if (refreshed)
    {
        display.synchronizeRefresh();
    }
}

//==============================================================================
void cDawControl::sendCc(uint8_t cc, uint8_t value)
{
    if (cc > 127)
        return;
    platform->getUsbMidi()->sendControlChange(cc, value, dawControlConfig.controlChannel);
}

void cDawControl::sendNote(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (note > 127)
        return;
    UsbMidi* usb = platform->getUsbMidi();
    if (velocity > 0)
        usb->sendNoteOn(note, velocity, channel);
    else
        usb->sendNoteOff(note, 0, channel);
}

void cDawControl::handleIncoming(uint8_t status, [[maybe_unused]] uint8_t channel, uint8_t data1,
                                 uint8_t data2)
{
    const DawControlConfig& cfg = dawControlConfig;

    if (!connected)
    {
        connected = true;
        infoDirty = true;
    }

    if (status == 0x90 || status == 0x80)
    {
        if (data1 < cfg.padNoteBase)
            return;
        uint8_t pad = data1 - cfg.padNoteBase;
        if (pad >= kPadCount)
            return;

        gridVelocity[pad] = (status == 0x80) ? 0 : data2;
        if (sessionMode)
        {
            applyGridLed(pad);
        }
        return;
    }

    if (status != 0xB0)
        return;

    if (data1 >= cfg.stateCcBase && data1 < cfg.stateCcBase + dawStateCount)
    {
        states[data1 - cfg.stateCcBase] = data2 > 0 ? 1 : 0;
        statesDirty = true;
    }
    else if (data1 == cfg.tempoMsbCc)
    {
        tempoMsb = data2;
    }
    else if (data1 == cfg.tempoLsbCc)
    {
        tempoTenths = (uint16_t)((tempoMsb << 7) | data2);
        infoDirty = true;
    }
    else if (data1 == cfg.ringTrackCc)
    {
        ringTrack = data2;
        infoDirty = true;
    }
    else if (data1 == cfg.ringSceneCc)
    {
        ringScene = data2;
        infoDirty = true;
    }
    else if (data1 == cfg.selectedTrackCc)
    {
        selectedTrack = data2;
        infoDirty = true;
    }
    else if (data1 == cfg.helloCc && data2 > 0)
    {
        // The script was (re)loaded: answer so it sends its state again.
        sendCc(cfg.helloCc, 127);
    }
}

//==============================================================================
void cDawControl::onButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress && state != buttonRelease)
        return;

    const DawControlConfig& cfg = dawControlConfig;
    bool press = (state == buttonPress);

    // Shift + Config always leaves the screen, whatever the JSON says.
    if (press && button == interfaceButtonConfig &&
        mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift))
    {
        exitScreen();
        return;
    }

    if (button == cfg.exitButton)
    {
        if (press)
            exitScreen();
        return;
    }

    if (button == cfg.modeButton)
    {
        if (press)
            toggleMode();
        return;
    }

    if (!sessionMode && (button == cfg.octaveUpButton || button == cfg.octaveDownButton))
    {
        if (press)
        {
            int8_t dir = (button == cfg.octaveUpButton) ? 1 : -1;
            if (octaveShift + dir >= -4 && octaveShift + dir <= 4)
            {
                releaseHeldNotes();
                octaveShift += dir;
                showNoteLeds();
                infoDirty = true;
            }
        }
        return;
    }

    sendCc(cfg.buttonCcBase + button, press ? 127 : 0);
}

void cDawControl::onPad(uint8_t pad, uint8_t state)
{
    if (pad >= kPadCount || (state != 0 && state != 1))
        return;

    const DawControlConfig& cfg = dawControlConfig;
    bool press = (state == 1);

    if (sessionMode)
    {
        sendNote(cfg.controlChannel, cfg.padNoteBase + pad, press ? cfg.padVelocity : 0);
        return;
    }

    if (press)
    {
        int16_t note = padToNote(pad);
        if (note < 0)
            return;
        if (heldNote[pad] != kNoNote)
            sendNote(cfg.noteChannel, heldNote[pad], 0);
        sendNote(cfg.noteChannel, (uint8_t)note, cfg.noteVelocity);
        heldNote[pad] = (uint8_t)note;
        setPadLight(pad, cfg.ledPressed, false);
    }
    else
    {
        if (heldNote[pad] != kNoNote)
        {
            sendNote(cfg.noteChannel, heldNote[pad], 0);
            heldNote[pad] = kNoNote;
        }
        setPadLight(pad, notePadLevel(pad), false);
    }
}

void cDawControl::onJog(int16_t value)
{
    if (value == 0)
        return;
    if (value > 63)
        value = 63;
    if (value < -63)
        value = -63;
    sendCc(dawControlConfig.jogCc, (uint8_t)(64 + value));
}

//==============================================================================
void cDawControl::setPadLight(uint8_t pad, uint8_t level, bool blink)
{
    if (level > 31)
        level = 31;

    if (blinking[pad])
    {
        padsBacklight.stopBlink(pad);
        blinking[pad] = false;
    }

    if (blink && level > 0)
    {
        padsBacklight.startBlink(level, pad);
        blinking[pad] = true;
    }
    else if (level > 0)
    {
        padsBacklight.setFrontLayer(1, level, pad);
    }
    else
    {
        padsBacklight.setFrontLayer(0, 0, pad);
    }
}

void cDawControl::applyGridLed(uint8_t pad)
{
    uint8_t v = gridVelocity[pad];
    if (v == 0)
        setPadLight(pad, 0, false);
    else if (v <= 31)
        setPadLight(pad, v, false);
    else if (v >= 33 && v <= 63)
        setPadLight(pad, v - 32, true);
    else
        setPadLight(pad, 31, false);
}

void cDawControl::showSessionLeds()
{
    for (uint8_t i = 0; i < kPadCount; i++)
        applyGridLed(i);
}

int16_t cDawControl::padToNote(uint8_t pad) const
{
    const DawControlConfig& cfg = dawControlConfig;
    uint8_t row = pad / kPadColumns; // 0 = top row
    uint8_t col = pad % kPadColumns;
    int16_t note = (int16_t)cfg.noteRoot + octaveShift * 12 +
                   (int16_t)(kPadRows - 1 - row) * cfg.noteRowOffset + col;
    if (note < 0 || note > 127)
        return -1;
    return note;
}

uint8_t cDawControl::notePadLevel(uint8_t pad) const
{
    const DawControlConfig& cfg = dawControlConfig;
    int16_t note = padToNote(pad);
    if (note < 0)
        return 0;

    int16_t pitchClass = ((note - cfg.noteRoot) % 12 + 12) % 12;
    if (pitchClass == 0)
        return cfg.ledRoot;
    for (uint8_t i = 0; i < cfg.scaleLength; i++)
    {
        if (cfg.scale[i] == pitchClass)
            return cfg.ledInScale;
    }
    return 0;
}

void cDawControl::showNoteLeds()
{
    for (uint8_t i = 0; i < kPadCount; i++)
        setPadLight(i, heldNote[i] != kNoNote ? dawControlConfig.ledPressed : notePadLevel(i), false);
}

void cDawControl::releaseHeldNotes()
{
    for (uint8_t i = 0; i < kPadCount; i++)
    {
        if (heldNote[i] != kNoNote)
        {
            sendNote(dawControlConfig.noteChannel, heldNote[i], 0);
            heldNote[i] = kNoNote;
        }
    }
}

void cDawControl::toggleMode()
{
    releaseHeldNotes();
    sessionMode = !sessionMode;
    if (sessionMode)
        showSessionLeds();
    else
        showNoteLeds();

    infoDirty = true;
    refreshBottomLabels();
    display.synchronizeRefresh();
}

void cDawControl::exitScreen()
{
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

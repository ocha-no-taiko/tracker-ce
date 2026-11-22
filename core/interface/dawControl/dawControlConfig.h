#ifndef INTERFACE_DAWCONTROL_DAWCONTROLCONFIG_H_
#define INTERFACE_DAWCONTROL_DAWCONTROLCONFIG_H_

#include <stddef.h>
#include <stdint.h>

// Settings for the DAW Control screen. Defaults are compiled in and can be
// overridden by /DawControl.json on the SD card (see sd-card/DawControl.json).
//
// Protocol summary (all on controlChannel unless noted):
//   Tracker -> DAW
//     button id N pressed/released : CC (buttonCcBase + N), 127 / 0
//     pad P (session mode)         : Note (padNoteBase + P), padVelocity / note off
//     pad P (note mode)            : Note on noteChannel, computed from the layout
//     jog wheel                    : CC jogCc, 64 + delta (relative)
//     screen opened / closed       : CC helloCc, 127 / 0
//   DAW -> Tracker
//     pad LED                      : Note (padNoteBase + P), velocity:
//                                    0 = off, 1..31 = steady, 33..63 = blink (v - 32),
//                                    anything else = full brightness
//     state                        : CC stateCcBase + DawState index, 0 = off, >0 = on
//     tempo                        : CC tempoMsbCc / tempoLsbCc, tempo * 10 as 14 bit
//     session ring position        : CC ringTrackCc / ringSceneCc (0-based offset)
//     selected track               : CC selectedTrackCc (0-based)
//     time signature               : CC signatureNumeratorCc / signatureDenominatorCc
//     texts for the screen         : SysEx F0 7D 50 54 <command> <index> <ASCII text> F7
//                                    (see DawSysEx below)

enum DawState : uint8_t
{
    dawStatePlay = 0,
    dawStateArrangementRecord,
    dawStateSessionRecord,
    dawStateOverdub,
    dawStateMetronome,
    dawStateCount
};

// SysEx commands from the DAW. 7D is the manufacturer id for non-commercial
// use, "PT" (50 54) marks the messages as ours.
enum DawSysEx : uint8_t
{
    dawSysExFileName = 1,    // text: name of the Live Set file ("" = not saved yet)
    dawSysExProjectName = 2, // text: name of its project folder
    dawSysExHelpLine = 3,    // index: line, text: "key<TAB>value" ("" = blank line);
                             // index 0 starts a new list
};

struct DawControlConfig
{
    static constexpr uint8_t kLabelCount = 8;
    static constexpr uint8_t kLabelLength = 16;
    static constexpr uint8_t kMaxScale = 12;
    static constexpr uint8_t kNoButton = 255;
    static constexpr uint8_t kInfoCount = 5; // File Name, Project Name, BPM, Beat, Metro
    static constexpr uint8_t kInfoLabelLength = 16;

    uint8_t controlChannel; // 1..16
    uint8_t noteChannel;    // 1..16

    uint8_t buttonCcBase;
    uint8_t jogCc;
    uint8_t helloCc;
    uint8_t padNoteBase;
    uint8_t padVelocity;

    uint8_t stateCcBase;
    uint8_t tempoMsbCc;
    uint8_t tempoLsbCc;
    uint8_t ringTrackCc;
    uint8_t ringSceneCc;
    uint8_t selectedTrackCc;
    uint8_t signatureNumeratorCc;
    uint8_t signatureDenominatorCc;

    // Note mode
    uint8_t noteRoot;      // MIDI note of the bottom-left pad
    uint8_t noteRowOffset; // semitones between rows
    uint8_t noteVelocity;
    uint8_t scale[kMaxScale]; // intervals from the root, used for pad lighting
    uint8_t scaleLength;
    uint8_t ledRoot;
    uint8_t ledInScale;
    uint8_t ledPressed;

    // Buttons handled by the Tracker itself (not sent to the DAW)
    uint8_t exitButton;
    uint8_t modeButton;
    uint8_t octaveUpButton;   // note mode only
    uint8_t octaveDownButton; // note mode only

    char labels[kLabelCount][kLabelLength];

    // Screen ("screen" in the JSON). Texts are UTF-8; ASCII and the
    // copyright sign are drawn, other characters show as '?'.
    char fontPath[48]; // loaded while the screen is open; "" = keep the UI font
    char title[32];
    char tagline[40];
    char credit[32];
    char description[96];
    char disclaimer[160];
    char moreDetail[112];
    char infoTitle[16];
    char helpTitle[16];
    char infoLabels[kInfoCount][kInfoLabelLength];
    char waitingText[40];  // file name line before the DAW answers
    char unsavedText[24];  // file name line for a Live Set that is not saved yet
    char metroOn[8];
    char metroOff[8];
    uint32_t backgroundColor; // 0xRRGGBB
    uint32_t textColor;
    uint32_t subTextColor;    // description, disclaimer, link, credit
    uint32_t lineColor;

    bool loadedFromSd;
};

extern DawControlConfig dawControlConfig;

void dawControlConfigSetDefaults(DawControlConfig& cfg);

// Parses JSON text into cfg. Unknown keys are ignored; keys that are present
// override the current values. Returns false on a syntax error.
bool dawControlConfigParse(const char* json, size_t len, DawControlConfig& cfg);

// Resets to defaults, then applies /DawControl.json when it exists.
void dawControlConfigLoad();

#endif /* INTERFACE_DAWCONTROL_DAWCONTROLCONFIG_H_ */

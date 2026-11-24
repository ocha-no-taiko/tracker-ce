#ifndef INTERFACE_Q4_Q4CONFIG_H_
#define INTERFACE_Q4_Q4CONFIG_H_

#include <stddef.h>
#include <stdint.h>

// Settings of the Q-4 screen (a sound-sculpting sampler made of the Tracker
// and Ableton Live). Defaults are compiled in and can be overridden by
// /Q4Control.json on the SD card (sample: sd-card/Q4Control.json).
//
// The Tracker only draws and sends what the buttons do; the pages (labels,
// gauges, values) come from the Q4 remote script in Live, which owns the
// parameter list (q4.json on the Live side). All on controlChannel:
//
// Tracker -> Live : CC (see Q4Cc), value as noted.
// Live -> Tracker : SysEx F0 7D 51 34 <command> <data...> F7 (see Q4SysEx).
//                   7D = non-commercial manufacturer id, "Q4" = 51 34.
// Both ways       : CC helloCc, 127 = screen opened / script (re)started,
//                   0 = screen closed. On 127 Live sends the whole page.

enum Q4Cc : uint8_t
{
    q4CcTab = 20,         // value: tab 0..5 (button under the screen; again = next page / slot)
    q4CcDevice = 21,      // value: tab 0..4 (Shift + tab: next device of the slot, BYPASS included)
    q4CcTrack = 22,       // value: track 0..3
    q4CcCursor = 23,      // value: selected cell 0..11
    q4CcJog = 24,         // value: 64 + steps (coarse), selected cell
    q4CcJogFine = 25,     // value: 64 + steps (Shift + jog)
    q4CcPress = 26,       // value: cell (action cells: pressed)
    q4CcRelease = 27,     // value: cell (action cells: released)
    q4CcMode = 28,        // value: cell (parameter cells: next mode)
    q4CcReset = 29,       // value: cell (back to the default value)
    q4CcMapJog = 30,      // value: 64 + steps; modulation depth of the selected cell
    q4CcMapPolarity = 31, // value: cell; unipolar <-> bipolar modulation
    q4CcMapMode = 32,     // value: 127 = MOD held (mapping mode), 0 = released
    q4CcPlay = 33,        // value: 127 = play / stop
    q4CcRecord = 34,      // value: 127 = RECORD of the Material slot
    q4CcGate = 35,        // value: 127 = a pad is down, 0 = all pads up (ADSR trigger)
    q4CcRefresh = 36,     // value: anything; Live sends the page again
};

enum Q4SysEx : uint8_t
{
    // Live -> Tracker
    // 01 page: track, tab, page, pageCount, slot, flags (bit0 bypassed, bit1 mapping mode),
    //          panel, then title (ASCII)
    q4SysExPage = 0x01,
    // 02 cell: index, kind, flags, value hi, value lo, mod hi, mod lo, icon,
    //          then "label<TAB>value text<TAB>mode text"
    q4SysExCell = 0x02,
    // 03 value: index, flags, value hi, value lo, mod hi, mod lo, then "value text<TAB>mode text"
    q4SysExValue = 0x03,
    // 04 status: track, scene hi, scene lo, bpm x10 (3 x 7 bit), flags (bit0 playing, bit1 recording),
    //            then the sample name
    q4SysExStatus = 0x04,
    // 05 wave begin: column count hi, lo (0 = no waveform)
    q4SysExWaveBegin = 0x05,
    // 06 wave data: first column hi, lo, then (min, max) pairs 0..127 (64 = silence)
    q4SysExWaveData = 0x06,
    // 07 markers: start, end, loop start, playhead (each 14 bit, 0..16383 of the
    //             sample; playhead 16383 = none), then up to 16 slice positions
    q4SysExMarkers = 0x07,
    // 08 notice: text for the title bar (a few seconds)
    q4SysExNotice = 0x08,
};

// Kinds of a cell in q4SysExCell
enum Q4CellKind : uint8_t
{
    q4CellEmpty = 0,
    q4CellUnipolar, // bar filled from the left
    q4CellBipolar,  // bar filled from the middle
    q4CellBox,      // value text in a box (select lists, times, grids)
    q4CellAction,   // button of the action row
};

enum Q4CellFlag : uint8_t
{
    q4FlagOn = 1 << 0,        // action: toggled on (drawn filled)
    q4FlagModes = 1 << 1,     // parameter: has modes (Insert cycles them)
    q4FlagModulated = 1 << 2, // has a modulation of the current slot (mod value is valid)
    q4FlagDisabled = 1 << 3,  // drawn dimmed, does nothing
    q4FlagNoteTrig = 1 << 4,  // action: the Tracker sends a note on the track channel while held
};

// Icons in place of an action label
enum Q4Icon : uint8_t
{
    q4IconNone = 0,
    q4IconLoopOff,      // one arrow
    q4IconLoopForward,  // two arrows
    q4IconLoopPingPong, // forward and backward arrow
};

// What is drawn in the left pane of a device page (q4SysExPage)
enum Q4Panel : uint8_t
{
    q4PanelEmpty = 0,
    q4PanelWave,  // Material: waveform of the sample
    q4PanelColor, // two hatched circles
    q4PanelSpace, // circle and box
};

enum Q4Tab : uint8_t
{
    q4TabMaterial = 0,
    q4TabGranular,
    q4TabFilter,
    q4TabColor,
    q4TabSpace,
    q4TabMod,
    q4TabHelp,
    q4TabQuit,
    q4TabCount
};

// Help pages (tiles of the Help tab, top-left first)
enum Q4HelpPage : uint8_t
{
    q4HelpMaterial = 0,
    q4HelpGranular,
    q4HelpFilter,
    q4HelpColor,
    q4HelpSpace,
    q4HelpMod,
    q4HelpCommands,
    q4HelpAbout,
    q4HelpCredit,
    q4HelpCount
};

struct Q4Config
{
    static constexpr uint8_t kHelpLines = 32;
    static constexpr uint8_t kHelpLineLength = 240;
    static constexpr uint8_t kCreditLines = 8;
    static constexpr uint8_t kCreditLineLength = 32;

    uint8_t controlChannel; // 1..16
    uint8_t helloCc;

    // Buttons (ids of interfaceDefs.h). Screen buttons 1..8 are the tabs.
    uint8_t trackButtons[4];  // select track 1..4
    uint8_t pressButton;      // press the action / next mode of the selected cell
    uint8_t resetButton;      // back to the default value
    uint8_t playButton;       // Live play / stop
    uint8_t recordButton;     // RECORD of the Material slot
    uint8_t exitButton;       // Shift + this leaves the screen at once
    uint8_t padNoteBase;      // note of the bottom-left pad
    uint8_t padVelocity;      // 1..127
    uint8_t trigNote;         // note sent by TRIG actions

    // Texts. UTF-8; ASCII and the copyright sign are drawn, others show as '?'.
    char fontPath[48];
    char title[12];
    char tagline[24];
    char tabLabels[q4TabCount][12]; // labels under the screen (and the title bar)
    char waiting[32];
    char bypassed[16];
    char mapping[24];      // title-bar text in mapping mode ("%d" = slot)
    char quitBefore[40];
    char quitWord[12];
    char quitAfter[24];
    char cancel[12];
    char navHint[48];
    char scrollHint[32];
    char helpTitles[q4HelpCount][16];
    // Help bodies: lines; "# ..." is a heading, "" an empty line, others wrap.
    char help[q4HelpCount - 1][kHelpLines][kHelpLineLength];
    char creditLines[kCreditLines][kCreditLineLength];
    char copyright[48];

    uint32_t backgroundColor; // 0xRRGGBB
    uint32_t textColor;
    uint32_t subTextColor;
    uint32_t lineColor;
    uint32_t selectColor;
    uint32_t quitColor;
    uint32_t dimColor;
    uint32_t modColor;

    bool loadedFromSd;
};

extern Q4Config q4Config;

void q4ConfigSetDefaults(Q4Config& cfg);

// Parses JSON text into cfg. Unknown keys are ignored; keys that are present
// override the current values. Returns false on a syntax error.
bool q4ConfigParse(const char* json, size_t len, Q4Config& cfg);

// Resets to defaults, then applies /Q4Control.json when it exists.
void q4ConfigLoad();

#endif /* INTERFACE_Q4_Q4CONFIG_H_ */

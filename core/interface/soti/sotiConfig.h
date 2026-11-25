#ifndef INTERFACE_SOTI_SOTICONFIG_H_
#define INTERFACE_SOTI_SOTICONFIG_H_

#include <stddef.h>
#include <stdint.h>

// Settings of the Synthesis of the Images screen: the Tracker as the panel of
// soti-server on the Mac (three depth-layered images and their sounds).
// Defaults are compiled in and can be overridden by /SotiControl.json on the
// SD card (sample: sd-card/SotiControl.json).
//
// The Tracker keeps the cursors and draws; the server owns the file list, the
// three slots and the 15 values. All on controlChannel. The numbers must match
// soti/protocol.py of the synthesis-of-the-images repository.
//
// Tracker -> server : CC (see SotiCc).
// Server -> Tracker : SysEx F0 7D 53 4F <command> <data...> F7 (see SotiSysEx).
//                     7D = non-commercial manufacturer id, "SO" = 53 4F.
// Both ways         : CC helloCc, 127 = screen opened / server started,
//                     0 = screen closed. On 127 the server sends everything.

enum SotiCc : uint8_t
{
    sotiCcFocus = 20,    // 0 = files, 1 = controls
    sotiCcFileHi = 21,   // upper 7 bits of the file cursor (sent first)
    sotiCcFileLo = 22,   // lower 7 bits; sets the file cursor
    sotiCcControl = 23,  // control cursor 0..14 (row * 3 + column)
    sotiCcJog = 24,      // 64 + steps
    sotiCcJogFine = 25,  // 64 + steps (Shift + jog)
    sotiCcLoad = 26,     // 0 = A, 1 = B, 2 = C, 3 = all three
    sotiCcSelect = 27,   // 1 = row, 2 = column, 3 = all (again = back to one)
    sotiCcReset = 28,    // selected values back to their defaults
    sotiCcPlay = 29,     // pause / resume the sound
    sotiCcRefresh = 30,  // send everything again
};

enum SotiSysEx : uint8_t
{
    sotiSysExFiles = 0x01,  // count hi, lo
    sotiSysExFile = 0x02,   // index hi, lo, flags (SotiFileFlag), name
    sotiSysExValues = 0x03, // 15 x (hi, lo), row-major (Layer A B C, Chroma A B C, ...)
    sotiSysExStatus = 0x04, // select mode, flags (SotiStatusFlag), "A<TAB>B<TAB>C" names
    sotiSysExNotice = 0x05, // text for the title bar
};

enum SotiFileFlag : uint8_t
{
    sotiFileA = 1 << 0,
    sotiFileB = 1 << 1,
    sotiFileC = 1 << 2,
    sotiFilePending = 1 << 3, // waiting for the depth analysis
    sotiFileError = 1 << 4,   // the analysis failed
};

enum SotiStatusFlag : uint8_t
{
    sotiStatusPlaying = 1 << 0,
    sotiStatusAnalyzing = 1 << 1,
};

enum SotiSelect : uint8_t
{
    sotiSelectOne = 0,
    sotiSelectRow,
    sotiSelectColumn,
    sotiSelectAll,
};

struct SotiConfig
{
    static constexpr uint8_t kParams = 5;
    static constexpr uint8_t kSlots = 3;

    uint8_t controlChannel; // 1..16
    uint8_t helloCc;

    // Buttons (ids of interfaceDefs.h). Screen buttons 1..8 are the actions below the screen.
    uint8_t filesButton;    // focus the file pane
    uint8_t controlsButton; // focus the control pane
    uint8_t resetButton;    // selected values back to their defaults
    uint8_t playButton;     // pause / resume the sound
    uint8_t exitButton;     // Shift + this leaves the screen at once
    uint16_t quitMs;        // QUIT must be pressed again within this time

    // Texts: ASCII (others show as '?').
    char fontPath[48];
    char title[32];
    char filesHeading[48];
    char controlsHeading[48];
    char filesHint[48];
    char controlsHint[48];
    char labels[8][16];   // below the screen
    char quitAgain[16];   // label 8 after the first press
    char params[kParams][12];
    char slots[kSlots][4];
    char waiting[40];
    char noFiles[40];
    char paused[16];
    char analyzing[16];

    uint32_t backgroundColor; // 0xRRGGBB
    uint32_t textColor;
    uint32_t subTextColor;
    uint32_t lineColor;
    uint32_t selectColor;  // the cursor of the focused pane
    uint32_t selectedFill; // cells the jog changes together
    uint32_t markColor;    // A B C marks on the files
    uint32_t dimColor;
    uint32_t quitColor;

    bool loadedFromSd;
};

extern SotiConfig sotiConfig;

void sotiConfigSetDefaults(SotiConfig& cfg);

// Parses JSON text into cfg. Unknown keys are ignored; keys that are present
// override the current values. Returns false on a syntax error.
bool sotiConfigParse(const char* json, size_t len, SotiConfig& cfg);

// Resets to defaults, then applies /SotiControl.json when it exists.
void sotiConfigLoad();

#endif /* INTERFACE_SOTI_SOTICONFIG_H_ */

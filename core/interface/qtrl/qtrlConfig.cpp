#include "qtrl/qtrlConfig.h"

#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"
#include "dawControl/miniJson.h"

// External RAM: the texts make the settings too big for DTC RAM.
__NOINIT(EXTERNAL_RAM) QtrlConfig qtrlConfig;

static constexpr const char* kConfigPath = "/QtrlControl.json";

__NOINIT(EXTERNAL_RAM) static char s_jsonBuffer[12288];
__NOINIT(EXTERNAL_RAM) static QtrlConfig s_parsed;

namespace
{
using namespace miniJson;

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

constexpr const char* kActionNames[qtrlActionCount] = {
    "none", "home", "status", "logs", "chat", "help", "quit", "undo", "redo", "config", "tabPrev", "tabNext",
};

uint8_t actionFromName(const char* name)
{
    for (uint8_t i = 0; i < qtrlActionCount; i++)
    {
        if (strcmp(kActionNames[i], name) == 0)
            return i;
    }
    return qtrlActionNone;
}

bool buttonFromName(const char* name, uint8_t& out)
{
    for (const ButtonName& b : kButtonNames)
    {
        if (strcmp(b.name, name) == 0)
        {
            out = b.id;
            return true;
        }
    }
    return false;
}

void addBinding(QtrlConfig& cfg, uint8_t button, bool shift, uint8_t action)
{
    // a later entry for the same key replaces the earlier one
    for (uint8_t i = 0; i < cfg.bindingCount; i++)
    {
        if (cfg.bindings[i].button == button && cfg.bindings[i].shift == shift)
        {
            cfg.bindings[i].action = action;
            return;
        }
    }
    if (cfg.bindingCount < QtrlConfig::kBindings)
        cfg.bindings[cfg.bindingCount++] = { button, shift, action };
}

// "Left", "Shift+Left", ...
bool parseKeyName(const char* key, uint8_t& button, bool& shift)
{
    shift = strncmp(key, "Shift+", 6) == 0;
    return buttonFromName(shift ? key + 6 : key, button);
}

void readAction(Reader& r, uint8_t& out)
{
    char name[16];
    if (r.peek('"') && r.readString(name, sizeof(name)))
        out = actionFromName(name);
    else if (r.readNull())
        out = qtrlActionNone;
    else
        r.skipValue();
}

void readBrightness(Reader& r, uint8_t& out)
{
    int v;
    if (r.readInt(v))
        out = clampU8(v, 0, 31);
    else
        r.skipValue();
}

// [["key", "value"], ...] -> rows; missing rows become empty.
template <size_t Rows, size_t KeyLength, size_t ValueLength>
void readPairs(Reader& r, char (&keys)[Rows][KeyLength], char (&values)[Rows][ValueLength])
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    memset(keys, 0, sizeof(keys));
    memset(values, 0, sizeof(values));
    uint8_t row = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (row < Rows && r.match('['))
        {
            uint8_t col = 0;
            while (!r.peek(']') && r.pos < r.len && !r.error)
            {
                if (col == 0 && r.peek('"'))
                    r.readString(keys[row], KeyLength);
                else if (col == 1 && r.peek('"'))
                    r.readString(values[row], ValueLength);
                else
                    r.skipValue();
                col++;
                r.match(',');
            }
            r.match(']');
        }
        else
        {
            r.skipValue();
        }
        row++;
        r.match(',');
    }
    r.match(']');
}

// [["1", "2", ...], ...] (4 rows of 12 keys)
void readKeyRows(Reader& r, char (&keys)[QtrlConfig::kKeyRows][QtrlConfig::kKeyColumns][QtrlConfig::kKeyLength])
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    uint8_t row = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (row < QtrlConfig::kKeyRows)
            readTextArray(r, keys[row]);
        else
            r.skipValue();
        row++;
        r.match(',');
    }
    r.match(']');
}

void readMarked(Reader& r, QtrlConfig& cfg)
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    cfg.markedCount = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        int v;
        if (r.readInt(v))
        {
            if (v >= 0 && v < 48 && cfg.markedCount < QtrlConfig::kMarkedKeys)
                cfg.markedKeys[cfg.markedCount++] = (uint8_t)v;
        }
        else
        {
            r.skipValue();
        }
        r.match(',');
    }
    r.match(']');
}

void readButtonByName(Reader& r, uint8_t& out)
{
    readButton(r, out);
}
} // namespace

//==============================================================================
void qtrlConfigSetDefaults(QtrlConfig& cfg)
{
    memset(&cfg, 0, sizeof(cfg));

    // ch16 is the DAW Control screen's; the server listens on ch15 only.
    cfg.controlChannel = 15;
    cfg.helloCc = 119;

    // The key list of the Help tab in the design.
    addBinding(cfg, interfaceButtonLeft, true, qtrlActionTabPrev);
    addBinding(cfg, interfaceButtonRight, true, qtrlActionTabNext);
    addBinding(cfg, interfaceButtonInsert, true, qtrlActionHome);
    addBinding(cfg, interfaceButtonPerformance, false, qtrlActionStatus);
    addBinding(cfg, interfaceButtonFile, false, qtrlActionLogs);
    addBinding(cfg, interfaceButtonConfig, false, qtrlActionConfig);
    addBinding(cfg, interfaceButtonParams, false, qtrlActionHelp);
    addBinding(cfg, interfaceButtonLeft, false, qtrlActionUndo);
    addBinding(cfg, interfaceButtonRight, false, qtrlActionRedo);
    addBinding(cfg, interfaceButtonDelete, false, qtrlActionQuit);

    static constexpr uint8_t kScreenActions[8] = {
        qtrlActionHome, qtrlActionStatus, qtrlActionLogs, qtrlActionChat,
        qtrlActionHelp, qtrlActionUndo,   qtrlActionRedo, qtrlActionQuit,
    };
    memcpy(cfg.screenButtons, kScreenActions, sizeof(kScreenActions));
    cfg.sendButton = interfaceButtonPlay;
    cfg.exitButton = interfaceButtonConfig;

    copyText(cfg.fontPath, "/Qtrl.ptfont");
    copyText(cfg.title, "Q.T.R.L.");
    copyText(cfg.tagline, "Quick Terminal for Remote & Logs");
    copyText(cfg.credit, "\xC2\xA9" "2026 Tusq");
    static constexpr const char* kHeadings[qtrlTabCount] = { "Home", "Status", "Log", "Chat", "Help", "QUIT" };
    for (uint8_t i = 0; i < qtrlTabCount; i++)
        copyText(cfg.headings[i], kHeadings[i]);
    static constexpr const char* kLabels[8] = { "Home", "Status", "Logs", "Chat", "Help", "Undo", "Redo", "QUIT" };
    for (uint8_t i = 0; i < 8; i++)
        copyText(cfg.labels[i], kLabels[i]);

    copyText(cfg.welcome, "Welcome to Q.T.R.L. !!");
    copyText(cfg.intro, "Select a tab using the buttons at the bottom of the screen.");
    static constexpr const char* kHome[][2] = {
        { "Home:", "This tab." },
        { "Status:", "Check the current system status." },
        { "Logs:", "Check the system logs." },
        { "Chat:", "Talk to \"qoala\"" },
        { "Help:", "View commands & key bindings." },
        { "Undo:", "Undo the last action." },
        { "Redo:", "Redo an undone action." },
        { "QUIT:", "Quit Q.T.R.L." },
    };
    for (uint8_t i = 0; i < sizeof(kHome) / sizeof(kHome[0]); i++)
    {
        copyText(cfg.homeKeys[i], kHome[i][0]);
        copyText(cfg.homeValues[i], kHome[i][1]);
    }
    static constexpr const char* kHelp[][2] = {
        { "Switch tabs:", "Shift+Arrow" },
        { "Home:", "Shift+Insert (Home)" },
        { "Status:", "Perform" },
        { "Logs:", "File" },
        { "Config:", "Config" },
        { "Help:", "Instrument Parameters" },
        { "Undo:", "Left Arrow" },
        { "Redo:", "Right Arrow" },
        { "QUIT:", "Delete" },
    };
    for (uint8_t i = 0; i < sizeof(kHelp) / sizeof(kHelp[0]); i++)
    {
        copyText(cfg.helpKeys[i], kHelp[i][0]);
        copyText(cfg.helpValues[i], kHelp[i][1]);
    }

    copyText(cfg.systemLabel, "Entire system:");
    copyText(cfg.checkDetail, "Please Check Detail");
    copyText(cfg.waiting, "Waiting for the server...");
    static constexpr const char* kNodes[QtrlConfig::kNodeCount][2] = {
        { "Terminal", "Polyend Tracker" }, { "QTRL1", "Arduino Uno Q" }, { "QTRL2", "ESP32-CAM" },
        { "FAB1", "Bambu Lab A1 mini" },   { "Server", "MacBook Pro" },
    };
    for (uint8_t i = 0; i < QtrlConfig::kNodeCount; i++)
    {
        copyText(cfg.nodeNames[i], kNodes[i][0]);
        copyText(cfg.nodeSubs[i], kNodes[i][1]);
    }
    copyText(cfg.statusUnknown, "No Report.");
    copyText(cfg.logsHint, "* Use arrows to move");
    copyText(cfg.logHint, "* Scroll using the jog wheel");
    copyText(cfg.logEmpty, "No logs yet.");
    copyText(cfg.logLoading, "Loading...");
    copyText(cfg.chatHint, "* Type on Pads, send with Play. Insert a line break with Shift+Play.");
    copyText(cfg.chatBusy, "qoala is thinking...");
    copyText(cfg.quitBefore, "Are you sure you want to ");
    copyText(cfg.quitWord, "QUIT");
    copyText(cfg.quitAfter, " the system?");
    copyText(cfg.cancel, "Cancel");
    copyText(cfg.cancelSub, "(Back to Home)");

    static constexpr const char* kKeys[QtrlConfig::kKeyRows][QtrlConfig::kKeyColumns] = {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "BS", "BS" },
        { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "(", ")" },
        { "a", "s", "d", "f", "g", "h", "j", "k", "l", ":", "CAPS", "CAPS" },
        { "z", "x", "c", "v", "b", "n", "m", ",", ".", "?", "SP", "SP" },
    };
    static constexpr const char* kShiftKeys[QtrlConfig::kKeyRows][QtrlConfig::kKeyColumns] = {
        { "!", "@", "#", "$", "%", "&", "*", "-", "+", "=", "BS", "BS" },
        { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]" },
        { "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "CAPS", "CAPS" },
        { "Z", "X", "C", "V", "B", "N", "M", "'", "\"", "/", "SP", "SP" },
    };
    for (uint8_t r = 0; r < QtrlConfig::kKeyRows; r++)
    {
        for (uint8_t c = 0; c < QtrlConfig::kKeyColumns; c++)
        {
            copyText(cfg.keys[r][c], kKeys[r][c]);
            copyText(cfg.shiftKeys[r][c], kShiftKeys[r][c]);
        }
    }
    cfg.markedKeys[0] = 2 * QtrlConfig::kKeyColumns + 3; // f
    cfg.markedKeys[1] = 2 * QtrlConfig::kKeyColumns + 6; // j
    cfg.markedCount = 2;
    cfg.ledKey = 3;
    cfg.ledMarked = 16;
    cfg.ledPressed = 31;

    cfg.backgroundColor = 0x121212;
    cfg.textColor = 0xFFFFFF;
    cfg.subTextColor = 0xFFFFFF;
    cfg.lineColor = 0xFFFFFF;
    cfg.okColor = 0x00FF80;
    cfg.alertColor = 0xFF0000;
    cfg.unknownColor = 0x808080;
    cfg.selectColor = 0xFFFF00;
    cfg.quitColor = 0xFF0000;

    cfg.loadedFromSd = false;
}

bool qtrlConfigParse(const char* json, size_t len, QtrlConfig& cfg)
{
    Reader r{ json, len, 0, false };

    readObject(r, [&](const char* key) {
        if (strcmp(key, "controlChannel") == 0)
        {
            int v;
            if (r.readInt(v))
                cfg.controlChannel = clampU8(v, 1, 16);
            else
                r.skipValue();
        }
        else if (strcmp(key, "helloCc") == 0)
        {
            int v;
            if (r.readInt(v))
                cfg.helloCc = clampU8(v, 0, 127);
            else
                r.skipValue();
        }
        else if (strcmp(key, "bindings") == 0)
        {
            // the JSON list replaces the built-in one
            bool first = true;
            readObject(r, [&](const char* k) {
                if (first)
                {
                    cfg.bindingCount = 0;
                    first = false;
                }
                uint8_t button;
                bool shift;
                uint8_t action = qtrlActionNone;
                readAction(r, action);
                if (parseKeyName(k, button, shift))
                    addBinding(cfg, button, shift, action);
            });
        }
        else if (strcmp(key, "screenButtons") == 0)
        {
            if (!r.match('['))
            {
                r.skipValue();
                return;
            }
            uint8_t i = 0;
            while (!r.peek(']') && r.pos < r.len && !r.error)
            {
                uint8_t action = qtrlActionNone;
                readAction(r, action);
                if (i < 8)
                    cfg.screenButtons[i] = action;
                i++;
                r.match(',');
            }
            r.match(']');
        }
        else if (strcmp(key, "sendButton") == 0)
            readButtonByName(r, cfg.sendButton);
        else if (strcmp(key, "exitButton") == 0)
            readButtonByName(r, cfg.exitButton);
        else if (strcmp(key, "labels") == 0)
            readTextArray(r, cfg.labels);
        else if (strcmp(key, "keyboard") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "rows") == 0)
                    readKeyRows(r, cfg.keys);
                else if (strcmp(k, "shiftRows") == 0)
                    readKeyRows(r, cfg.shiftKeys);
                else if (strcmp(k, "marked") == 0)
                    readMarked(r, cfg);
                else if (strcmp(k, "ledKey") == 0)
                    readBrightness(r, cfg.ledKey);
                else if (strcmp(k, "ledMarked") == 0)
                    readBrightness(r, cfg.ledMarked);
                else if (strcmp(k, "ledPressed") == 0)
                    readBrightness(r, cfg.ledPressed);
                else
                    r.skipValue();
            });
        }
        else if (strcmp(key, "screen") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "font") == 0)
                    readText(r, cfg.fontPath);
                else if (strcmp(k, "title") == 0)
                    readText(r, cfg.title);
                else if (strcmp(k, "tagline") == 0)
                    readText(r, cfg.tagline);
                else if (strcmp(k, "credit") == 0)
                    readText(r, cfg.credit);
                else if (strcmp(k, "headings") == 0)
                    readTextArray(r, cfg.headings);
                else if (strcmp(k, "welcome") == 0)
                    readText(r, cfg.welcome);
                else if (strcmp(k, "intro") == 0)
                    readText(r, cfg.intro);
                else if (strcmp(k, "home") == 0)
                    readPairs(r, cfg.homeKeys, cfg.homeValues);
                else if (strcmp(k, "help") == 0)
                    readPairs(r, cfg.helpKeys, cfg.helpValues);
                else if (strcmp(k, "systemLabel") == 0)
                    readText(r, cfg.systemLabel);
                else if (strcmp(k, "checkDetail") == 0)
                    readText(r, cfg.checkDetail);
                else if (strcmp(k, "waiting") == 0)
                    readText(r, cfg.waiting);
                else if (strcmp(k, "nodeNames") == 0)
                    readTextArray(r, cfg.nodeNames);
                else if (strcmp(k, "nodeSubs") == 0)
                    readTextArray(r, cfg.nodeSubs);
                else if (strcmp(k, "statusUnknown") == 0)
                    readText(r, cfg.statusUnknown);
                else if (strcmp(k, "logsHint") == 0)
                    readText(r, cfg.logsHint);
                else if (strcmp(k, "logHint") == 0)
                    readText(r, cfg.logHint);
                else if (strcmp(k, "logEmpty") == 0)
                    readText(r, cfg.logEmpty);
                else if (strcmp(k, "logLoading") == 0)
                    readText(r, cfg.logLoading);
                else if (strcmp(k, "chatHint") == 0)
                    readText(r, cfg.chatHint);
                else if (strcmp(k, "chatBusy") == 0)
                    readText(r, cfg.chatBusy);
                else if (strcmp(k, "quitBefore") == 0)
                    readText(r, cfg.quitBefore);
                else if (strcmp(k, "quitWord") == 0)
                    readText(r, cfg.quitWord);
                else if (strcmp(k, "quitAfter") == 0)
                    readText(r, cfg.quitAfter);
                else if (strcmp(k, "cancel") == 0)
                    readText(r, cfg.cancel);
                else if (strcmp(k, "cancelSub") == 0)
                    readText(r, cfg.cancelSub);
                else if (strcmp(k, "colors") == 0)
                {
                    readObject(r, [&](const char* c) {
                        if (strcmp(c, "background") == 0)
                            readColor(r, cfg.backgroundColor);
                        else if (strcmp(c, "text") == 0)
                            readColor(r, cfg.textColor);
                        else if (strcmp(c, "subText") == 0)
                            readColor(r, cfg.subTextColor);
                        else if (strcmp(c, "line") == 0)
                            readColor(r, cfg.lineColor);
                        else if (strcmp(c, "ok") == 0)
                            readColor(r, cfg.okColor);
                        else if (strcmp(c, "alert") == 0)
                            readColor(r, cfg.alertColor);
                        else if (strcmp(c, "unknown") == 0)
                            readColor(r, cfg.unknownColor);
                        else if (strcmp(c, "select") == 0)
                            readColor(r, cfg.selectColor);
                        else if (strcmp(c, "quit") == 0)
                            readColor(r, cfg.quitColor);
                        else
                            r.skipValue();
                    });
                }
                else
                    r.skipValue();
            });
        }
        else
            r.skipValue();
    });

    return !r.error;
}

void qtrlConfigLoad()
{
    qtrlConfigSetDefaults(qtrlConfig);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr || !fs->isSdCardInserted() || !fs->exists(kConfigPath))
        return;

    size_t bytesRead = 0;
    if (!SdTextFile::readAll(fs, kConfigPath, s_jsonBuffer, sizeof(s_jsonBuffer), &bytesRead))
        return;

    s_parsed = qtrlConfig;
    if (qtrlConfigParse(s_jsonBuffer, bytesRead, s_parsed))
    {
        s_parsed.loadedFromSd = true;
        qtrlConfig = s_parsed;
    }
}

uint8_t qtrlConfigAction(const QtrlConfig& cfg, uint8_t button, bool shift)
{
    for (uint8_t i = 0; i < cfg.bindingCount; i++)
    {
        if (cfg.bindings[i].button == button && cfg.bindings[i].shift == shift)
            return cfg.bindings[i].action;
    }
    if (!shift && button <= interfaceButton7)
        return cfg.screenButtons[button - interfaceButton0];
    return qtrlActionNone;
}

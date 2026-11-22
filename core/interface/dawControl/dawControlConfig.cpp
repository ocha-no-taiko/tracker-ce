#include "dawControl/dawControlConfig.h"

#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"

// External RAM: the screen texts make the settings too big for DTC RAM.
// Set by dawControlConfigLoad() before the screen uses them.
__NOINIT(EXTERNAL_RAM) DawControlConfig dawControlConfig;

static constexpr const char* kConfigPath = "/DawControl.json";

__NOINIT(EXTERNAL_RAM) static char s_jsonBuffer[8192];
__NOINIT(EXTERNAL_RAM) static DawControlConfig s_parsed;

template <size_t N>
static void copyText(char (&dst)[N], const char* src)
{
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

// Button names accepted in the JSON in place of numeric ids.
struct ButtonName
{
    const char* name;
    uint8_t id;
};

static constexpr ButtonName kButtonNames[] = {
    { "Screen1", interfaceButton0 },
    { "Screen2", interfaceButton1 },
    { "Screen3", interfaceButton2 },
    { "Screen4", interfaceButton3 },
    { "Screen5", interfaceButton4 },
    { "Screen6", interfaceButton5 },
    { "Screen7", interfaceButton6 },
    { "Screen8", interfaceButton7 },
    { "Params", interfaceButtonParams },
    { "Performance", interfaceButtonPerformance },
    { "File", interfaceButtonFile },
    { "Config", interfaceButtonConfig },
    { "Master", interfaceButtonMaster },
    { "SampleLoad", interfaceButtonSampleLoad },
    { "SamplePlay", interfaceButtonSamplePlay },
    { "SampleEdit", interfaceButtonSampleEdit },
    { "SampleRec", interfaceButtonSampleRec },
    { "Song", interfaceButtonSong },
    { "Note", interfaceButtonNote },
    { "Instr", interfaceButtonInstr },
    { "Vol", interfaceButtonVol },
    { "Fx", interfaceButtonFx },
    { "Pattern", interfaceButtonPattern },
    { "Play", interfaceButtonPlay },
    { "Copy", interfaceButtonCopy },
    { "Insert", interfaceButtonInsert },
    { "Up", interfaceButtonUp },
    { "Delete", interfaceButtonDelete },
    { "Rec", interfaceButtonRec },
    { "Shift", interfaceButtonShift },
    { "Left", interfaceButtonLeft },
    { "Down", interfaceButtonDown },
    { "Right", interfaceButtonRight },
};

void dawControlConfigSetDefaults(DawControlConfig& cfg)
{
    memset(&cfg, 0, sizeof(cfg));

    cfg.controlChannel = 16;
    cfg.noteChannel = 1;

    cfg.buttonCcBase = 20;
    cfg.jogCc = 70;
    cfg.helloCc = 119;
    cfg.padNoteBase = 36;
    cfg.padVelocity = 127;

    cfg.stateCcBase = 100;
    cfg.tempoMsbCc = 110;
    cfg.tempoLsbCc = 111;
    cfg.ringTrackCc = 112;
    cfg.ringSceneCc = 113;
    cfg.selectedTrackCc = 114;
    cfg.signatureNumeratorCc = 115;
    cfg.signatureDenominatorCc = 116;

    cfg.noteRoot = 36;
    cfg.noteRowOffset = 12;
    cfg.noteVelocity = 100;
    static constexpr uint8_t kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
    memcpy(cfg.scale, kMajor, sizeof(kMajor));
    cfg.scaleLength = sizeof(kMajor);
    cfg.ledRoot = 20;
    cfg.ledInScale = 4;
    cfg.ledPressed = 31;

    cfg.exitButton = interfaceButton7;
    cfg.modeButton = interfaceButton6;
    cfg.octaveUpButton = interfaceButtonUp;
    cfg.octaveDownButton = interfaceButtonDown;

    static constexpr const char* kLabels[DawControlConfig::kLabelCount] = {
        "Act", "Solo", "Rec Arm", "Undo", "Redo", "Metro", "", "EXIT",
    };
    for (uint8_t i = 0; i < DawControlConfig::kLabelCount; i++)
        copyText(cfg.labels[i], kLabels[i]);

    copyText(cfg.fontPath, "/LiveTracker.ptfont");
    copyText(cfg.title, "Live Tracker");
    copyText(cfg.tagline, "Don't push, Track.");
    copyText(cfg.credit, "\xC2\xA9" "2026 Tusq.");
    copyText(cfg.description, "A system for controlling Ableton Live with Polyend Tracker.");
    copyText(cfg.disclaimer,
             "* This firmware is unofficial. Therefore, we cannot accept responsibility "
             "for any damage that may occur as a result of using it.");
    copyText(cfg.moreDetail, "More Detail: https://github.com/ocha-no-taiko/tracker-ce/tree/daw-control");
    copyText(cfg.infoTitle, "> info");
    copyText(cfg.helpTitle, "> help");
    static constexpr const char* kInfoLabels[DawControlConfig::kInfoCount] = {
        "File Name:", "Project Name:", "BPM:", "Beat:", "Metro:",
    };
    for (uint8_t i = 0; i < DawControlConfig::kInfoCount; i++)
        copyText(cfg.infoLabels[i], kInfoLabels[i]);
    copyText(cfg.waitingText, "Waiting for Live...");
    copyText(cfg.unsavedText, "(not saved)");
    copyText(cfg.metroOn, "On");
    copyText(cfg.metroOff, "Off");
    cfg.backgroundColor = 0x121212;
    cfg.textColor = 0xFFFFFF;
    cfg.subTextColor = 0xFFFFFF;
    cfg.lineColor = 0xFFFFFF;

    cfg.loadedFromSd = false;
}

//==============================================================================
// Minimal zero-allocation JSON reader (same approach as ThemeManager's reader,
// extended with numbers and arrays).
namespace
{
struct Reader
{
    const char* json;
    size_t len;
    size_t pos;
    bool error;

    void skipWs()
    {
        while (pos < len &&
               (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
            pos++;
    }

    bool match(char c)
    {
        skipWs();
        if (pos < len && json[pos] == c)
        {
            pos++;
            return true;
        }
        return false;
    }

    bool peek(char c)
    {
        skipWs();
        return pos < len && json[pos] == c;
    }

    bool readString(char* out, size_t maxLen)
    {
        skipWs();
        if (pos >= len || json[pos] != '"')
            return false;
        pos++;
        size_t idx = 0;
        while (pos < len && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < len)
                pos++;
            if (idx + 1 < maxLen)
                out[idx++] = json[pos];
            pos++;
        }
        if (pos >= len)
        {
            error = true;
            return false;
        }
        pos++;
        out[idx] = '\0';
        return true;
    }

    bool readInt(int& out)
    {
        skipWs();
        bool negative = false;
        if (pos < len && json[pos] == '-')
        {
            negative = true;
            pos++;
        }
        if (pos >= len || json[pos] < '0' || json[pos] > '9')
            return false;
        int value = 0;
        while (pos < len && json[pos] >= '0' && json[pos] <= '9')
        {
            if (value < 100000)
                value = value * 10 + (json[pos] - '0');
            pos++;
        }
        out = negative ? -value : value;
        return true;
    }

    bool readNull()
    {
        skipWs();
        if (pos + 4 <= len && strncmp(&json[pos], "null", 4) == 0)
        {
            pos += 4;
            return true;
        }
        return false;
    }

    void skipValue()
    {
        skipWs();
        if (pos >= len)
            return;
        char c = json[pos];
        if (c == '"')
        {
            char dummy[4];
            readString(dummy, sizeof(dummy));
        }
        else if (c == '{' || c == '[')
        {
            char open = c;
            char close = (c == '{') ? '}' : ']';
            pos++;
            int depth = 1;
            while (pos < len && depth > 0)
            {
                if (json[pos] == '"')
                {
                    char dummy[4];
                    readString(dummy, sizeof(dummy));
                    continue;
                }
                if (json[pos] == open)
                    depth++;
                else if (json[pos] == close)
                    depth--;
                pos++;
            }
        }
        else
        {
            while (pos < len && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
                pos++;
        }
    }
};

uint8_t clampU8(int v, int lo, int hi)
{
    if (v < lo)
        return (uint8_t)lo;
    if (v > hi)
        return (uint8_t)hi;
    return (uint8_t)v;
}

void readMidiValue(Reader& r, uint8_t& out)
{
    int v;
    if (r.readInt(v))
        out = clampU8(v, 0, 127);
    else
        r.skipValue();
}

void readChannel(Reader& r, uint8_t& out)
{
    int v;
    if (r.readInt(v))
        out = clampU8(v, 1, 16);
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

// A button may be given as a number, a name ("Up", "Screen8", ...) or null.
void readButton(Reader& r, uint8_t& out)
{
    if (r.readNull())
    {
        out = DawControlConfig::kNoButton;
        return;
    }
    if (r.peek('"'))
    {
        char name[16];
        if (!r.readString(name, sizeof(name)))
            return;
        for (const ButtonName& b : kButtonNames)
        {
            if (strcmp(b.name, name) == 0)
            {
                out = b.id;
                return;
            }
        }
        return; // unknown name: keep previous value
    }
    int v;
    if (r.readInt(v))
    {
        if (v >= 0 && v < interfaceButtonsCount)
            out = (uint8_t)v;
    }
    else
    {
        r.skipValue();
    }
}

template <size_t N>
void readText(Reader& r, char (&out)[N])
{
    if (r.peek('"'))
        r.readString(out, N);
    else
        r.skipValue();
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "#RRGGBB" (or "RRGGBB"); anything else keeps the previous color.
void readColor(Reader& r, uint32_t& out)
{
    char text[12];
    if (!r.peek('"'))
    {
        r.skipValue();
        return;
    }
    if (!r.readString(text, sizeof(text)))
        return;
    const char* p = (text[0] == '#') ? text + 1 : text;
    if (strlen(p) != 6)
        return;
    uint32_t value = 0;
    for (uint8_t i = 0; i < 6; i++)
    {
        int d = hexDigit(p[i]);
        if (d < 0)
            return;
        value = (value << 4) | (uint32_t)d;
    }
    out = value;
}

template <typename Fn>
bool readObject(Reader& r, Fn onKey)
{
    if (!r.match('{'))
    {
        r.skipValue();
        return false;
    }
    while (!r.peek('}') && r.pos < r.len && !r.error)
    {
        char key[32];
        if (!r.readString(key, sizeof(key)))
        {
            r.error = true;
            return false;
        }
        if (!r.match(':'))
        {
            r.error = true;
            return false;
        }
        onKey(key);
        r.match(',');
    }
    if (!r.match('}'))
        r.error = true;
    return !r.error;
}

void readScale(Reader& r, DawControlConfig& cfg)
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    uint8_t count = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        int v;
        if (r.readInt(v))
        {
            if (count < DawControlConfig::kMaxScale)
                cfg.scale[count++] = clampU8(v, 0, 11);
        }
        else
        {
            r.skipValue();
        }
        r.match(',');
    }
    r.match(']');
    cfg.scaleLength = count;
}

template <size_t Count, size_t Length>
void readTextArray(Reader& r, char (&texts)[Count][Length])
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    uint8_t idx = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (idx < Count && r.peek('"'))
        {
            r.readString(texts[idx], Length);
        }
        else
        {
            r.skipValue();
        }
        idx++;
        r.match(',');
    }
    r.match(']');
}
} // namespace

bool dawControlConfigParse(const char* json, size_t len, DawControlConfig& cfg)
{
    Reader r{ json, len, 0, false };

    readObject(r, [&](const char* key) {
        if (strcmp(key, "controlChannel") == 0)
            readChannel(r, cfg.controlChannel);
        else if (strcmp(key, "noteChannel") == 0)
            readChannel(r, cfg.noteChannel);
        else if (strcmp(key, "buttonCcBase") == 0)
            readMidiValue(r, cfg.buttonCcBase);
        else if (strcmp(key, "jogCc") == 0)
            readMidiValue(r, cfg.jogCc);
        else if (strcmp(key, "helloCc") == 0)
            readMidiValue(r, cfg.helloCc);
        else if (strcmp(key, "padNoteBase") == 0)
            readMidiValue(r, cfg.padNoteBase);
        else if (strcmp(key, "padVelocity") == 0)
            readMidiValue(r, cfg.padVelocity);
        else if (strcmp(key, "feedback") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "stateCcBase") == 0)
                    readMidiValue(r, cfg.stateCcBase);
                else if (strcmp(k, "tempoMsbCc") == 0)
                    readMidiValue(r, cfg.tempoMsbCc);
                else if (strcmp(k, "tempoLsbCc") == 0)
                    readMidiValue(r, cfg.tempoLsbCc);
                else if (strcmp(k, "ringTrackCc") == 0)
                    readMidiValue(r, cfg.ringTrackCc);
                else if (strcmp(k, "ringSceneCc") == 0)
                    readMidiValue(r, cfg.ringSceneCc);
                else if (strcmp(k, "selectedTrackCc") == 0)
                    readMidiValue(r, cfg.selectedTrackCc);
                else if (strcmp(k, "signatureNumeratorCc") == 0)
                    readMidiValue(r, cfg.signatureNumeratorCc);
                else if (strcmp(k, "signatureDenominatorCc") == 0)
                    readMidiValue(r, cfg.signatureDenominatorCc);
                else
                    r.skipValue();
            });
        }
        else if (strcmp(key, "noteMode") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "root") == 0)
                    readMidiValue(r, cfg.noteRoot);
                else if (strcmp(k, "rowOffset") == 0)
                    readMidiValue(r, cfg.noteRowOffset);
                else if (strcmp(k, "velocity") == 0)
                    readMidiValue(r, cfg.noteVelocity);
                else if (strcmp(k, "scale") == 0)
                    readScale(r, cfg);
                else if (strcmp(k, "ledRoot") == 0)
                    readBrightness(r, cfg.ledRoot);
                else if (strcmp(k, "ledInScale") == 0)
                    readBrightness(r, cfg.ledInScale);
                else if (strcmp(k, "ledPressed") == 0)
                    readBrightness(r, cfg.ledPressed);
                else
                    r.skipValue();
            });
        }
        else if (strcmp(key, "localButtons") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "exit") == 0)
                    readButton(r, cfg.exitButton);
                else if (strcmp(k, "mode") == 0)
                    readButton(r, cfg.modeButton);
                else if (strcmp(k, "octaveUp") == 0)
                    readButton(r, cfg.octaveUpButton);
                else if (strcmp(k, "octaveDown") == 0)
                    readButton(r, cfg.octaveDownButton);
                else
                    r.skipValue();
            });
        }
        else if (strcmp(key, "labels") == 0)
            readTextArray(r, cfg.labels);
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
                else if (strcmp(k, "description") == 0)
                    readText(r, cfg.description);
                else if (strcmp(k, "disclaimer") == 0)
                    readText(r, cfg.disclaimer);
                else if (strcmp(k, "moreDetail") == 0)
                    readText(r, cfg.moreDetail);
                else if (strcmp(k, "infoTitle") == 0)
                    readText(r, cfg.infoTitle);
                else if (strcmp(k, "helpTitle") == 0)
                    readText(r, cfg.helpTitle);
                else if (strcmp(k, "infoLabels") == 0)
                    readTextArray(r, cfg.infoLabels);
                else if (strcmp(k, "waiting") == 0)
                    readText(r, cfg.waitingText);
                else if (strcmp(k, "notSaved") == 0)
                    readText(r, cfg.unsavedText);
                else if (strcmp(k, "metroOn") == 0)
                    readText(r, cfg.metroOn);
                else if (strcmp(k, "metroOff") == 0)
                    readText(r, cfg.metroOff);
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

void dawControlConfigLoad()
{
    dawControlConfigSetDefaults(dawControlConfig);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr || !fs->isSdCardInserted() || !fs->exists(kConfigPath))
        return;

    size_t bytesRead = 0;
    if (!SdTextFile::readAll(fs, kConfigPath, s_jsonBuffer, sizeof(s_jsonBuffer), &bytesRead))
        return;

    s_parsed = dawControlConfig;
    if (dawControlConfigParse(s_jsonBuffer, bytesRead, s_parsed))
    {
        s_parsed.loadedFromSd = true;
        dawControlConfig = s_parsed;
    }
}

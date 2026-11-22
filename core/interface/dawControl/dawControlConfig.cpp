#include "dawControl/dawControlConfig.h"

#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"

DawControlConfig dawControlConfig;

static constexpr const char* kConfigPath = "/DawControl.json";

__NOINIT(EXTERNAL_RAM) static char s_jsonBuffer[4096];

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
        "Act", "Solo", "Rec Arm", "Undo", "Redo", "Metro", "", "Exit",
    };
    for (uint8_t i = 0; i < DawControlConfig::kLabelCount; i++)
    {
        strncpy(cfg.labels[i], kLabels[i], DawControlConfig::kLabelLength - 1);
        cfg.labels[i][DawControlConfig::kLabelLength - 1] = '\0';
    }

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

void readLabels(Reader& r, DawControlConfig& cfg)
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    uint8_t idx = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (idx < DawControlConfig::kLabelCount && r.peek('"'))
        {
            r.readString(cfg.labels[idx], DawControlConfig::kLabelLength);
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
            readLabels(r, cfg);
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

    DawControlConfig parsed = dawControlConfig;
    if (dawControlConfigParse(s_jsonBuffer, bytesRead, parsed))
    {
        parsed.loadedFromSd = true;
        dawControlConfig = parsed;
    }
}

#ifndef INTERFACE_DAWCONTROL_MINIJSON_H_
#define INTERFACE_DAWCONTROL_MINIJSON_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/interfaceDefs.h"

// Small JSON reading helpers shared by the settings of the DAW Control and
// Q.T.R.L. screens (DawControl.json, QtrlControl.json). No allocation.
namespace miniJson
{
constexpr uint8_t kNoButton = 255;

// Button names accepted in the JSON in place of numeric ids.
struct ButtonName
{
    const char* name;
    uint8_t id;
};

inline constexpr ButtonName kButtonNames[] = {
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

// Minimal zero-allocation JSON reader (same approach as ThemeManager's reader,
// extended with numbers and arrays).
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

inline uint8_t clampU8(int v, int lo, int hi)
{
    if (v < lo)
        return (uint8_t)lo;
    if (v > hi)
        return (uint8_t)hi;
    return (uint8_t)v;
}

// A button may be given as a number, a name ("Up", "Screen8", ...) or null.
inline void readButton(Reader& r, uint8_t& out)
{
    if (r.readNull())
    {
        out = kNoButton;
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
inline void readText(Reader& r, char (&out)[N])
{
    if (r.peek('"'))
        r.readString(out, N);
    else
        r.skipValue();
}

inline int hexDigit(char c)
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
inline void readColor(Reader& r, uint32_t& out)
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
inline bool readObject(Reader& r, Fn onKey)
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

template <size_t Count, size_t Length>
inline void readTextArray(Reader& r, char (&texts)[Count][Length])
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

} // namespace miniJson

#endif /* INTERFACE_DAWCONTROL_MINIJSON_H_ */

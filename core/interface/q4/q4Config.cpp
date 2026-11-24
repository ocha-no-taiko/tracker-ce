#include "q4/q4Config.h"

#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"
#include "dawControl/miniJson.h"

// External RAM: the help texts make the settings too big for DTC RAM.
__NOINIT(EXTERNAL_RAM) Q4Config q4Config;

static constexpr const char* kConfigPath = "/Q4Control.json";

__NOINIT(EXTERNAL_RAM) static char s_jsonBuffer[49152];
__NOINIT(EXTERNAL_RAM) static Q4Config s_parsed;

namespace
{
using namespace miniJson;

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

// Built-in help texts, made from sd-card/Q4Control.json
// (tools/q4/make_help_defaults.py), so both say the same.
#include "q4/q4HelpDefaults.inc"

void readInt(Reader& r, uint8_t& out, int lo, int hi)
{
    int v;
    if (r.readInt(v))
        out = clampU8(v, lo, hi);
    else
        r.skipValue();
}

// ["line", ...] -> lines; missing lines become empty.
template <size_t Lines, size_t Length>
void readLines(Reader& r, char (&lines)[Lines][Length])
{
    if (!r.match('['))
    {
        r.skipValue();
        return;
    }
    memset(lines, 0, sizeof(lines));
    size_t i = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (i < Lines && r.peek('"'))
            r.readString(lines[i], Length);
        else
            r.skipValue();
        i++;
        r.match(',');
    }
    r.match(']');
}

uint8_t helpIndex(const char* key)
{
    static constexpr const char* kKeys[q4HelpCount] = {
        "material", "granular", "filter", "color", "space", "mod", "commands", "about", "credit",
    };
    for (uint8_t i = 0; i < q4HelpCount; i++)
    {
        if (strcmp(kKeys[i], key) == 0)
            return i;
    }
    return q4HelpCount;
}

void readHelp(Reader& r, Q4Config& cfg)
{
    readObject(r, [&](const char* key) {
        uint8_t page = helpIndex(key);
        if (page == q4HelpCredit)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "title") == 0)
                    readText(r, cfg.helpTitles[q4HelpCredit]);
                else if (strcmp(k, "lines") == 0)
                    readLines(r, cfg.creditLines);
                else if (strcmp(k, "copyright") == 0)
                    readText(r, cfg.copyright);
                else
                    r.skipValue();
            });
        }
        else if (page < q4HelpCount)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "title") == 0)
                    readText(r, cfg.helpTitles[page]);
                else if (strcmp(k, "lines") == 0)
                    readLines(r, cfg.help[page]);
                else
                    r.skipValue();
            });
        }
        else
            r.skipValue();
    });
}

void readButtons(Reader& r, Q4Config& cfg)
{
    readObject(r, [&](const char* k) {
        if (strcmp(k, "tracks") == 0)
        {
            if (!r.match('['))
            {
                r.skipValue();
                return;
            }
            uint8_t i = 0;
            while (!r.peek(']') && r.pos < r.len && !r.error)
            {
                uint8_t b = kNoButton;
                readButton(r, b);
                if (i < 4)
                    cfg.trackButtons[i] = b;
                i++;
                r.match(',');
            }
            r.match(']');
        }
        else if (strcmp(k, "press") == 0)
            readButton(r, cfg.pressButton);
        else if (strcmp(k, "reset") == 0)
            readButton(r, cfg.resetButton);
        else if (strcmp(k, "play") == 0)
            readButton(r, cfg.playButton);
        else if (strcmp(k, "record") == 0)
            readButton(r, cfg.recordButton);
        else if (strcmp(k, "exit") == 0)
            readButton(r, cfg.exitButton);
        else
            r.skipValue();
    });
}

void readColors(Reader& r, Q4Config& cfg)
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
        else if (strcmp(c, "select") == 0)
            readColor(r, cfg.selectColor);
        else if (strcmp(c, "quit") == 0)
            readColor(r, cfg.quitColor);
        else if (strcmp(c, "dim") == 0)
            readColor(r, cfg.dimColor);
        else if (strcmp(c, "mod") == 0)
            readColor(r, cfg.modColor);
        else
            r.skipValue();
    });
}
} // namespace

//==============================================================================
void q4ConfigSetDefaults(Q4Config& cfg)
{
    memset(&cfg, 0, sizeof(cfg));

    // ch16 = DAW Control (Live Tracker), ch15 = Q.T.R.L.
    cfg.controlChannel = 14;
    cfg.helloCc = 119;

    cfg.trackButtons[0] = interfaceButtonNote;
    cfg.trackButtons[1] = interfaceButtonInstr;
    cfg.trackButtons[2] = interfaceButtonVol;
    cfg.trackButtons[3] = interfaceButtonFx;
    cfg.pressButton = interfaceButtonInsert;
    cfg.resetButton = interfaceButtonDelete;
    cfg.playButton = interfaceButtonPlay;
    cfg.recordButton = interfaceButtonRec;
    cfg.exitButton = interfaceButtonConfig;
    cfg.padNoteBase = 36;
    cfg.padVelocity = 100;
    cfg.trigNote = 60;

    copyText(cfg.fontPath, "/Q4.ptfont");
    copyText(cfg.title, "Q-4");
    copyText(cfg.tagline, "squlpting Sampler");
    static constexpr const char* kTabs[q4TabCount] = {
        "Material", "Granular", "Filter", "Color", "Space", "MOD", "Help", "QUIT",
    };
    for (uint8_t i = 0; i < q4TabCount; i++)
        copyText(cfg.tabLabels[i], kTabs[i]);
    copyText(cfg.waiting, "Waiting for Live...");
    copyText(cfg.bypassed, "BYPASS");
    copyText(cfg.mapping, "MAPPING MOD%d");
    copyText(cfg.quitBefore, "Are you sure you want to ");
    copyText(cfg.quitWord, "QUIT");
    copyText(cfg.quitAfter, " the system?");
    copyText(cfg.cancel, "Cancel");
    copyText(cfg.navHint, "* Use the arrow keys to navigate between pages.");
    copyText(cfg.scrollHint, "* Scroll using the jog wheel");

    for (uint8_t page = 0; page < q4HelpCount; page++)
        copyText(cfg.helpTitles[page], kHelpTitles[page]);
    for (uint8_t page = 0; page + 1 < q4HelpCount; page++)
    {
        for (uint8_t i = 0; i < Q4Config::kHelpLines && kHelpLines[page][i] != nullptr; i++)
            copyText(cfg.help[page][i], kHelpLines[page][i]);
    }
    for (uint8_t i = 0; i < Q4Config::kCreditLines && kCreditLines[i] != nullptr; i++)
        copyText(cfg.creditLines[i], kCreditLines[i]);
    copyText(cfg.copyright, kCopyright);

    cfg.backgroundColor = 0x121212;
    cfg.textColor = 0xFFFFFF;
    cfg.subTextColor = 0xFFFFFF;
    cfg.lineColor = 0xFFFFFF;
    cfg.selectColor = 0xFFFF00;
    cfg.quitColor = 0xFF0000;
    cfg.dimColor = 0x606060;
    cfg.modColor = 0x00C8FF;

    cfg.loadedFromSd = false;
}

bool q4ConfigParse(const char* json, size_t len, Q4Config& cfg)
{
    Reader r{ json, len, 0, false };

    readObject(r, [&](const char* key) {
        if (strcmp(key, "controlChannel") == 0)
            readInt(r, cfg.controlChannel, 1, 16);
        else if (strcmp(key, "helloCc") == 0)
            readInt(r, cfg.helloCc, 0, 127);
        else if (strcmp(key, "buttons") == 0)
            readButtons(r, cfg);
        else if (strcmp(key, "pads") == 0)
        {
            readObject(r, [&](const char* k) {
                if (strcmp(k, "noteBase") == 0)
                    readInt(r, cfg.padNoteBase, 0, 127 - 47);
                else if (strcmp(k, "velocity") == 0)
                    readInt(r, cfg.padVelocity, 1, 127);
                else if (strcmp(k, "trigNote") == 0)
                    readInt(r, cfg.trigNote, 0, 127);
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
                else if (strcmp(k, "tabs") == 0)
                    readTextArray(r, cfg.tabLabels);
                else if (strcmp(k, "waiting") == 0)
                    readText(r, cfg.waiting);
                else if (strcmp(k, "bypassed") == 0)
                    readText(r, cfg.bypassed);
                else if (strcmp(k, "mapping") == 0)
                    readText(r, cfg.mapping);
                else if (strcmp(k, "quitBefore") == 0)
                    readText(r, cfg.quitBefore);
                else if (strcmp(k, "quitWord") == 0)
                    readText(r, cfg.quitWord);
                else if (strcmp(k, "quitAfter") == 0)
                    readText(r, cfg.quitAfter);
                else if (strcmp(k, "cancel") == 0)
                    readText(r, cfg.cancel);
                else if (strcmp(k, "navHint") == 0)
                    readText(r, cfg.navHint);
                else if (strcmp(k, "scrollHint") == 0)
                    readText(r, cfg.scrollHint);
                else if (strcmp(k, "colors") == 0)
                    readColors(r, cfg);
                else
                    r.skipValue();
            });
        }
        else if (strcmp(key, "help") == 0)
            readHelp(r, cfg);
        else
            r.skipValue();
    });

    return !r.error;
}

void q4ConfigLoad()
{
    q4ConfigSetDefaults(q4Config);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr || !fs->isSdCardInserted() || !fs->exists(kConfigPath))
        return;

    size_t bytesRead = 0;
    if (!SdTextFile::readAll(fs, kConfigPath, s_jsonBuffer, sizeof(s_jsonBuffer), &bytesRead))
        return;

    s_parsed = q4Config;
    if (q4ConfigParse(s_jsonBuffer, bytesRead, s_parsed))
    {
        s_parsed.loadedFromSd = true;
        q4Config = s_parsed;
    }
}

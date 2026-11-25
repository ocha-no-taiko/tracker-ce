// The 1 MB flash is nearly full and this screen is not time-critical.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include "soti/sotiConfig.h"

#include <stddef.h>
#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"
#include "dawControl/miniJson.h"

__NOINIT(EXTERNAL_RAM) SotiConfig sotiConfig;

static constexpr const char* kConfigPath = "/SotiControl.json";

__NOINIT(EXTERNAL_RAM) static char s_jsonBuffer[16384];
__NOINIT(EXTERNAL_RAM) static SotiConfig s_parsed;

namespace
{
using namespace miniJson;

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

void readInt(Reader& r, uint8_t& out, int lo, int hi)
{
    int v;
    if (r.readInt(v))
        out = clampU8(v, lo, hi);
    else
        r.skipValue();
}

void readMs(Reader& r, uint16_t& out)
{
    int v;
    if (r.readInt(v))
        out = (uint16_t)(v < 500 ? 500 : v > 10000 ? 10000 : v);
    else
        r.skipValue();
}

// The keys are tables, so the parser is one small loop instead of a template
// instance per text (the 1 MB flash is nearly full).
struct Field
{
    const char* key;
    uint16_t offset;
    uint8_t size;  // bytes of one text (texts), 0 for colors / buttons
    uint8_t count; // texts in a list (1 = one text)
};
#define SOTI_TEXT(key, f) { key, (uint16_t)offsetof(SotiConfig, f), (uint8_t)sizeof(SotiConfig::f), 1 }
#define SOTI_LIST(key, f)                                                                    \
    { key, (uint16_t)offsetof(SotiConfig, f), (uint8_t)sizeof(SotiConfig::f[0]),          \
      (uint8_t)(sizeof(SotiConfig::f) / sizeof(SotiConfig::f[0])) }
#define SOTI_PLAIN(key, f) { key, (uint16_t)offsetof(SotiConfig, f), 0, 1 }

constexpr Field kScreenFields[] = {
    SOTI_TEXT("font", fontPath),
    SOTI_TEXT("title", title),
    SOTI_TEXT("filesHeading", filesHeading),
    SOTI_TEXT("controlsHeading", controlsHeading),
    SOTI_TEXT("filesHint", filesHint),
    SOTI_TEXT("controlsHint", controlsHint),
    SOTI_LIST("labels", labels),
    SOTI_TEXT("quitAgain", quitAgain),
    SOTI_LIST("params", params),
    SOTI_LIST("slots", slots),
    SOTI_TEXT("waiting", waiting),
    SOTI_TEXT("noFiles", noFiles),
    SOTI_TEXT("paused", paused),
    SOTI_TEXT("analyzing", analyzing),
};
constexpr Field kColorFields[] = {
    SOTI_PLAIN("background", backgroundColor), SOTI_PLAIN("text", textColor),
    SOTI_PLAIN("subText", subTextColor),       SOTI_PLAIN("line", lineColor),
    SOTI_PLAIN("select", selectColor),         SOTI_PLAIN("selectedFill", selectedFill),
    SOTI_PLAIN("mark", markColor),             SOTI_PLAIN("dim", dimColor),
    SOTI_PLAIN("quit", quitColor),
};
constexpr Field kButtonFields[] = {
    SOTI_PLAIN("files", filesButton), SOTI_PLAIN("controls", controlsButton),
    SOTI_PLAIN("reset", resetButton), SOTI_PLAIN("play", playButton),
    SOTI_PLAIN("exit", exitButton),
};

template <size_t N>
const Field* findField(const Field (&fields)[N], const char* key)
{
    for (const Field& f : fields)
    {
        if (strcmp(f.key, key) == 0)
            return &f;
    }
    return nullptr;
}

// "text" or ["text", ...] into size-byte texts; a shorter list keeps the rest.
void readTexts(Reader& r, char* out, uint8_t size, uint8_t count)
{
    if (count == 1 || !r.peek('['))
    {
        if (count == 1 && r.peek('"'))
            r.readString(out, size);
        else
            r.skipValue();
        return;
    }
    r.match('[');
    uint8_t i = 0;
    while (!r.peek(']') && r.pos < r.len && !r.error)
    {
        if (i < count && r.peek('"'))
            r.readString(out + (size_t)i * size, size);
        else
            r.skipValue();
        i++;
        r.match(',');
    }
    r.match(']');
}

enum Section : uint8_t
{
    sectionScreen,
    sectionColors,
    sectionButtons,
};

void readSection(Reader& r, SotiConfig& cfg, Section section)
{
    uint8_t* base = (uint8_t*)&cfg;
    readObject(r, [&](const char* key) {
        if (section == sectionScreen && strcmp(key, "colors") == 0)
        {
            readSection(r, cfg, sectionColors);
            return;
        }
        const Field* f = section == sectionScreen   ? findField(kScreenFields, key)
                         : section == sectionColors ? findField(kColorFields, key)
                                                    : findField(kButtonFields, key);
        if (f == nullptr)
            r.skipValue();
        else if (section == sectionScreen)
            readTexts(r, (char*)(base + f->offset), f->size, f->count);
        else if (section == sectionColors)
            readColor(r, *(uint32_t*)(base + f->offset));
        else
            readButton(r, *(base + f->offset));
    });
}
} // namespace

//==============================================================================
void sotiConfigSetDefaults(SotiConfig& cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    // ch16 DAW Control, ch15 Q.T.R.L., ch14 Q-4
    cfg.controlChannel = 13;
    cfg.helloCc = 119;
    cfg.filesButton = interfaceButtonFile;
    cfg.controlsButton = interfaceButtonPerformance;
    cfg.resetButton = interfaceButtonDelete;
    cfg.playButton = interfaceButtonPlay;
    cfg.exitButton = interfaceButtonConfig;
    cfg.quitMs = 3000;

    copyText(cfg.fontPath, "/Soti.ptfont");
    copyText(cfg.title, "Synthesis of the Images");
    copyText(cfg.filesHeading, "> File: Focus the cursor with File.");
    copyText(cfg.controlsHeading, "> Controls: Focus the cursor with Perform.");
    copyText(cfg.filesHint, "* Select a file using the arrow keys.");
    copyText(cfg.controlsHint, "* Select a parameter using the arrow keys.");
    static constexpr const char* kLabels[8] = {
        "Load into A", "Load into B", "Load into C", "Load into All",
        "SLCT row",    "SLCT column", "SLCT All",    "QUIT",
    };
    for (uint8_t i = 0; i < 8; i++)
        copyText(cfg.labels[i], kLabels[i]);
    copyText(cfg.quitAgain, "QUIT?");
    static constexpr const char* kParams[SotiConfig::kParams] = { "Layer", "Chroma", "Blur", "Resolution", "Space" };
    for (uint8_t i = 0; i < SotiConfig::kParams; i++)
        copyText(cfg.params[i], kParams[i]);
    static constexpr const char* kSlots[SotiConfig::kSlots] = { "A", "B", "C" };
    for (uint8_t i = 0; i < SotiConfig::kSlots; i++)
        copyText(cfg.slots[i], kSlots[i]);
    copyText(cfg.waiting, "Waiting for soti-server...");
    copyText(cfg.noFiles, "No files (media folder on the Mac)");
    copyText(cfg.paused, "PAUSED");
    copyText(cfg.analyzing, "ANALYZING");

    cfg.backgroundColor = 0x121212;
    cfg.textColor = 0xFFFFFF;
    cfg.subTextColor = 0xB0B0B0;
    cfg.lineColor = 0xFFFFFF;
    cfg.selectColor = 0xFFD24A;
    cfg.selectedFill = 0x3A3A3A;
    cfg.markColor = 0xFFD24A;
    cfg.dimColor = 0x707070;
    cfg.quitColor = 0xFF5050;
    cfg.loadedFromSd = false;
}

bool sotiConfigParse(const char* json, size_t len, SotiConfig& cfg)
{
    Reader r{ json, len, 0, false };
    readObject(r, [&](const char* key) {
        if (strcmp(key, "controlChannel") == 0)
            readInt(r, cfg.controlChannel, 1, 16);
        else if (strcmp(key, "helloCc") == 0)
            readInt(r, cfg.helloCc, 0, 127);
        else if (strcmp(key, "quitMs") == 0)
            readMs(r, cfg.quitMs);
        else if (strcmp(key, "buttons") == 0)
            readSection(r, cfg, sectionButtons);
        else if (strcmp(key, "screen") == 0)
            readSection(r, cfg, sectionScreen);
        else
            r.skipValue();
    });
    return !r.error;
}

void sotiConfigLoad()
{
    sotiConfigSetDefaults(sotiConfig);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr || !fs->isSdCardInserted() || !fs->exists(kConfigPath))
        return;

    size_t bytesRead = 0;
    if (!SdTextFile::readAll(fs, kConfigPath, s_jsonBuffer, sizeof(s_jsonBuffer), &bytesRead))
        return;

    s_parsed = sotiConfig;
    if (sotiConfigParse(s_jsonBuffer, bytesRead, s_parsed))
    {
        s_parsed.loadedFromSd = true;
        sotiConfig = s_parsed;
    }
}

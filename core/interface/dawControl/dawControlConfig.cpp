#include "dawControl/dawControlConfig.h"

#include <string.h>

#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "core/interfaceDefs.h"
#include "dawControl/miniJson.h"

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
namespace
{
using namespace miniJson;

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

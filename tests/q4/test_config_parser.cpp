// Host-side test of the Q-4 JSON parser (no hardware needed).
#include <cassert>
#include <cstdio>
#include <cstring>

#include "core/interfaceDefs.h"
#include "q4/q4Config.h"

static Q4Config cfg;

static void check(bool ok, const char* what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what);
        assert(false);
    }
}

int main()
{
    q4ConfigSetDefaults(cfg);
    check(cfg.controlChannel == 14, "default channel");
    check(cfg.trackButtons[0] == interfaceButtonNote && cfg.trackButtons[3] == interfaceButtonFx, "track buttons");
    check(cfg.pressButton == interfaceButtonInsert, "press button");
    check(strcmp(cfg.tabLabels[7], "QUIT") == 0, "tab labels");
    check(strcmp(cfg.helpTitles[7], "About Q-4") == 0, "built-in help titles");
    check(strcmp(cfg.help[0][0], "# Overview") == 0, "built-in help text");
    check(strcmp(cfg.creditLines[1], "- Claude") == 0, "built-in credit");

    // the sample file parses and matches the built-in values
    FILE* f = fopen(SDJSON, "rb");
    check(f != nullptr, "sample JSON exists");
    static char json[65536];
    size_t n = fread(json, 1, sizeof(json), f);
    fclose(f);
    static Q4Config sample;
    q4ConfigSetDefaults(sample);
    check(q4ConfigParse(json, n, sample), "sample JSON parses");
    check(memcmp(sample.trackButtons, cfg.trackButtons, 4) == 0, "same tracks");
    check(sample.resetButton == cfg.resetButton && sample.exitButton == cfg.exitButton, "same buttons");
    for (uint8_t p = 0; p + 1 < q4HelpCount; p++)
        for (uint8_t i = 0; i < Q4Config::kHelpLines; i++)
            check(strcmp(sample.help[p][i], cfg.help[p][i]) == 0, "help same as built-in");
    check(strcmp(sample.copyright, cfg.copyright) == 0, "copyright");
    check(strncmp(sample.copyright, "\xC2\xA9" "2026", 6) == 0, "UTF-8 copyright");
    check(sample.modColor == 0x00C8FF, "colors");
    check(strcmp(sample.fontPath, "/Q4.ptfont") == 0, "font");

    // overrides
    const char* custom = "{\"controlChannel\": 3, \"buttons\": {\"tracks\": [\"Song\", null], \"press\": \"Copy\"},"
                         " \"pads\": {\"velocity\": 300},"
                         " \"screen\": {\"tabs\": [\"MAT\"], \"colors\": {\"select\": \"#010203\"}},"
                         " \"help\": {\"filter\": {\"title\": \"F\", \"lines\": [\"a\", \"b\"]}}}";
    static Q4Config c;
    q4ConfigSetDefaults(c);
    check(q4ConfigParse(custom, strlen(custom), c), "custom parses");
    check(c.controlChannel == 3, "channel override");
    check(c.trackButtons[0] == interfaceButtonSong && c.trackButtons[1] == 255, "tracks override");
    check(c.trackButtons[2] == interfaceButtonVol, "untouched track");
    check(c.pressButton == interfaceButtonCopy, "press override");
    check(c.padVelocity == 127, "velocity clamped");
    check(strcmp(c.tabLabels[0], "MAT") == 0 && strcmp(c.tabLabels[1], "Granular") == 0, "tab override");
    check(c.selectColor == 0x010203, "color override");
    check(strcmp(c.helpTitles[q4HelpFilter], "F") == 0, "help title");
    check(strcmp(c.help[q4HelpFilter][1], "b") == 0 && c.help[q4HelpFilter][2][0] == '\0', "help lines replaced");

    check(!q4ConfigParse("{\"screen\": [", 12, c), "syntax error detected");

    printf("all parser checks passed\n");
    return 0;
}

// Host-side test of the Synthesis of the Images JSON parser (no hardware needed).
#include <cassert>
#include <cstdio>
#include <cstring>

#include "core/interfaceDefs.h"
#include "soti/sotiConfig.h"

static SotiConfig cfg;

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
    sotiConfigSetDefaults(cfg);
    check(cfg.controlChannel == 13, "default channel");
    check(cfg.filesButton == interfaceButtonFile, "files button");
    check(cfg.controlsButton == interfaceButtonPerformance, "controls button");
    check(strcmp(cfg.labels[2], "Load into C") == 0, "labels");
    check(strcmp(cfg.params[3], "Resolution") == 0, "params");

    // the sample on the SD card says the same as the defaults
    FILE* f = fopen(SDJSON, "rb");
    check(f != nullptr, "sample JSON exists");
    static char json[16384];
    size_t n = fread(json, 1, sizeof(json), f);
    fclose(f);
    static SotiConfig fromSd;
    sotiConfigSetDefaults(fromSd);
    check(sotiConfigParse(json, n, fromSd), "sample JSON parses");
    fromSd.loadedFromSd = false;
    check(memcmp(&fromSd, &cfg, sizeof(cfg)) == 0, "sample JSON = built-in defaults");

    // overrides and bad input
    const char* custom = "{\"controlChannel\": 3, \"quitMs\": 99999, \"buttons\": {\"files\": \"Song\", \"play\": null},"
                         " \"screen\": {\"labels\": [\"L1\"], \"colors\": {\"select\": \"#00FF00\"}}, \"extra\": [1, 2]}";
    SotiConfig c = cfg;
    check(sotiConfigParse(custom, strlen(custom), c), "custom JSON parses");
    check(c.controlChannel == 3, "channel override");
    check(c.quitMs == 10000, "quitMs clamped");
    check(c.filesButton == interfaceButtonSong, "button by name");
    check(c.playButton == 255, "button null = none");
    check(strcmp(c.labels[0], "L1") == 0 && strcmp(c.labels[1], "Load into B") == 0, "partial label list");
    check(c.selectColor == 0x00FF00, "color");
    const char* broken = "{\"controlChannel\": ";
    SotiConfig b = cfg;
    check(!sotiConfigParse(broken, strlen(broken), b), "broken JSON is refused");

    printf("SotiControl.json parser: all checks passed\n");
    return 0;
}

// Host-side test of the Q.T.R.L. JSON parser (no hardware needed).
#include <cassert>
#include <cstdio>
#include <cstring>

#include "core/interfaceDefs.h"
#include "qtrl/qtrlConfig.h"

static QtrlConfig cfg;

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
    // defaults follow the design
    qtrlConfigSetDefaults(cfg);
    check(cfg.controlChannel == 15, "default channel");
    check(qtrlConfigAction(cfg, interfaceButton0, false) == qtrlActionHome, "Screen1 = home");
    check(qtrlConfigAction(cfg, interfaceButton7, false) == qtrlActionQuit, "Screen8 = quit");
    check(qtrlConfigAction(cfg, interfaceButtonLeft, true) == qtrlActionTabPrev, "Shift+Left");
    check(qtrlConfigAction(cfg, interfaceButtonLeft, false) == qtrlActionUndo, "Left = undo");
    check(qtrlConfigAction(cfg, interfaceButtonInsert, true) == qtrlActionHome, "Shift+Insert");
    check(qtrlConfigAction(cfg, interfaceButtonInsert, false) == qtrlActionNone, "Insert alone");
    check(strcmp(cfg.keys[2][10], "CAPS") == 0, "CAPS key text fits");
    check(cfg.markedCount == 2 && cfg.markedKeys[0] == 27, "home keys");

    // the sample file parses and matches the defaults where it repeats them
    FILE* f = fopen(SDJSON, "rb");
    check(f != nullptr, "sample JSON exists");
    static char json[16384];
    size_t n = fread(json, 1, sizeof(json), f);
    fclose(f);
    static QtrlConfig sample;
    qtrlConfigSetDefaults(sample);
    check(qtrlConfigParse(json, n, sample), "sample JSON parses");
    check(sample.bindingCount == cfg.bindingCount, "sample bindings");
    for (uint8_t b = 0; b < interfaceButtonsCount; b++)
    {
        check(qtrlConfigAction(sample, b, false) == qtrlConfigAction(cfg, b, false), "same action");
        check(qtrlConfigAction(sample, b, true) == qtrlConfigAction(cfg, b, true), "same shift action");
    }
    check(strcmp(sample.helpKeys[0], "Switch tabs:") == 0, "help rows");
    check(strcmp(sample.homeValues[7], "Quit Q.T.R.L.") == 0, "home rows");
    check(strcmp(sample.credit, "\xC2\xA9" "2026 Tusq") == 0, "UTF-8 credit");
    check(sample.okColor == 0x00FF80, "colors");
    check(strcmp(sample.shiftKeys[3][8], "\"") == 0, "escaped quote key");
    check(strcmp(sample.fontPath, "/Qtrl.ptfont") == 0, "font");

    // overrides
    const char* custom = "{\"controlChannel\": 3, \"bindings\": {\"Play\": \"chat\", \"Shift+Up\": \"help\"},"
                         " \"screenButtons\": [null, \"logs\"], \"keyboard\": {\"marked\": [1], \"ledKey\": 99},"
                         " \"screen\": {\"nodeNames\": [\"T\"], \"colors\": {\"ok\": \"#010203\"}}}";
    static QtrlConfig c;
    qtrlConfigSetDefaults(c);
    check(qtrlConfigParse(custom, strlen(custom), c), "custom parses");
    check(c.controlChannel == 3, "channel override");
    check(c.bindingCount == 2, "bindings replaced");
    check(qtrlConfigAction(c, interfaceButtonPlay, false) == qtrlActionChat, "Play = chat");
    check(qtrlConfigAction(c, interfaceButtonUp, true) == qtrlActionHelp, "Shift+Up = help");
    check(qtrlConfigAction(c, interfaceButtonLeft, false) == qtrlActionNone, "old binding gone");
    check(qtrlConfigAction(c, interfaceButton0, false) == qtrlActionNone, "null screen button");
    check(qtrlConfigAction(c, interfaceButton1, false) == qtrlActionLogs, "screen button 2");
    check(c.markedCount == 1 && c.markedKeys[0] == 1, "marked keys");
    check(c.ledKey == 31, "LED clamped");
    check(strcmp(c.nodeNames[0], "T") == 0 && strcmp(c.nodeNames[1], "QTRL1") == 0, "node names");
    check(c.okColor == 0x010203, "color override");

    check(!qtrlConfigParse("{\"labels\": [", 12, c), "syntax error detected");

    printf("all parser checks passed\n");
    return 0;
}

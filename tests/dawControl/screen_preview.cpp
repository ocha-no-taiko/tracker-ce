// Host-side preview of the DAW Control screen: runs the real drawing code of
// core/interface/dawControl/dawScreen.cpp against a stub display list, with
// the font metrics of LiveTracker.ptfont. preview_screen.py draws the list.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dawControl/dawScreen.h"

FILE* g_list = nullptr;
unsigned g_words = 0;
strFont fonts[5];

static bool loadFonts(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t header[16];
    if (fread(header, 1, 16, f) != 16 || memcmp(header, "PTFN", 4) != 0)
        return false;
    for (int i = 0; i < 5; i++)
    {
        uint32_t entry[3];
        fseek(f, 16 + i * 12, SEEK_SET);
        if (fread(entry, 4, 3, f) != 3)
            return false;
        uint8_t block[148];
        fseek(f, entry[0], SEEK_SET);
        if (fread(block, 1, 148, f) != 148)
            return false;
        memcpy(fonts[i].data, block, 128);
        fonts[i].width = block[136];
        fonts[i].height = block[140];
        fonts[i].handle = i + 1;
    }
    fclose(f);
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s font.ptfont DawControl.json out.txt\n", argv[0]);
        return 2;
    }
    if (!loadFonts(argv[1]))
    {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    static DawControlConfig cfg;
    dawControlConfigSetDefaults(cfg);
    FILE* jf = fopen(argv[2], "rb");
    static char json[8192];
    size_t n = jf ? fread(json, 1, sizeof(json), jf) : 0;
    if (jf)
        fclose(jf);
    if (!dawControlConfigParse(json, n, cfg))
    {
        fprintf(stderr, "JSON error\n");
        return 1;
    }

    static DawScreenData data;
    data.cfg = &cfg;
    data.dawFont = true;
    // argv[4] = "long": a name too long for its row (ends in "...")
    bool longNames = argc > 4 && strcmp(argv[4], "long") == 0;
    const char* info[5] = { longNames ? "A very long Live Set name for testing.als" : "Test.als",
                            longNames ? cfg.waitingText : "Test", "120", "4/4", "On" };
    for (int i = 0; i < 5; i++)
        snprintf(data.info[i], sizeof(data.info[i]), "%s", info[i]);
    dawScreenSetDefaultHelp(data);
    for (int i = 0; i < 8; i++)
        snprintf(data.labels[i], sizeof(data.labels[i]), "%s", i == 6 ? "> Note" : cfg.labels[i]);

    g_list = fopen(argv[3], "w");
    unsigned total = 0;
    const char* names[4] = { "background", "info", "help", "labels" };
    for (int part = 0; part < 4; part++)
    {
        strControlProperties prop;
        prop.value = part;
        prop.data = &data;
        cDawScreen screen(&prop);
        g_words = 0;
        screen.update();
        printf("%-10s %4u words\n", names[part], g_words);
        total += g_words;
    }
    fclose(g_list);
    printf("total      %4u words (a frame allows 2043)\n", total);
    return total <= 2043 ? 0 : 1;
}

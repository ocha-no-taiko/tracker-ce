// Host-side preview of the Synthesis of the Images screen: runs the real
// drawing code of core/interface/soti/sotiScreen.cpp against a stub display
// list, with the font metrics of Soti.ptfont. tests/dawControl/preview_screen.py
// draws it.
//   screen_preview font.ptfont SotiControl.json out_prefix
// writes out_prefix<shot>.txt for every shot and fails when a shot needs
// more display-list words than a frame allows.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "soti/sotiScreen.h"

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

static SotiScreenData data;

static void files(int count)
{
    data.fileCount = count;
    for (int i = 0; i < count; i++)
    {
        snprintf(data.fileNames[i], sizeof(data.fileNames[i]), "test%d", i + 1);
        data.fileFlags[i] = 0;
    }
}

static void values(const float (&v)[15])
{
    for (int i = 0; i < 15; i++)
        data.values[i] = (uint16_t)lroundf(v[i] * 16383.0f);
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s font.ptfont SotiControl.json out_prefix\n", argv[0]);
        return 2;
    }
    if (!loadFonts(argv[1]))
    {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    static SotiConfig cfg;
    sotiConfigSetDefaults(cfg);
    FILE* jf = fopen(argv[2], "rb");
    static char json[16384];
    size_t n = jf ? fread(json, 1, sizeof(json), jf) : 0;
    if (jf)
        fclose(jf);
    if (n > 0 && !sotiConfigParse(json, n, cfg))
    {
        fprintf(stderr, "JSON error\n");
        return 1;
    }
    data.cfg = &cfg;
    data.ownFont = true;
    sotiScreenReset(data);

    bool ok = true;
    FILE* sink = fopen("/dev/null", "w");
    auto render = [&](const char* name) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s.txt", argv[3], name);
        g_list = fopen(path, "w");
        unsigned total = 0;
        for (int part = 0; part < sotiScreenPartCount; part++)
        {
            strControlProperties prop;
            prop.value = part;
            prop.data = &data;
            cSotiScreen screen(&prop);
            g_words = 0;
            screen.update();
            total += g_words;

            // a stale handle poked by another module must not change the part
            FILE* list = g_list;
            g_list = sink;
            const unsigned words = g_words;
            cSotiScreen poked(&prop);
            poked.setValue(sotiScreenHeader);
            poked.setData(nullptr);
            g_words = 0;
            poked.update();
            g_list = list;
            if (g_words != words)
            {
                printf("%s: part %d changed after setValue/setData\n", name, part);
                ok = false;
            }
        }
        fclose(g_list);
        printf("%-16s %4u words (a frame allows 2043)\n", name, total);
        if (total > 2043)
            ok = false;
    };

    render("waiting");

    // the mockup: nine files, values as drawn there, file pane focused
    data.connected = true;
    files(9);
    values({ 0.08f, 0.85f, 0.45f, 0.55f, 0.3f, 0.12f, 0.45f, 0.12f, 0.85f, 0.85f, 0.55f, 0.45f, 0.55f, 0.45f, 0.12f });
    data.statusFlags = sotiStatusPlaying;
    snprintf(data.slotNames[0], sizeof(data.slotNames[0]), "test1");
    snprintf(data.slotNames[1], sizeof(data.slotNames[1]), "test3");
    snprintf(data.slotNames[2], sizeof(data.slotNames[2]), "-");
    data.fileFlags[0] = sotiFileA;
    data.fileFlags[2] = sotiFileB;
    data.focus = 0;
    data.fileCursor = 0;
    render("files");

    // controls focused, a whole row selected, a notice
    data.focus = 1;
    data.controlCursor = 7; // Blur_B
    data.select = sotiSelectRow;
    snprintf(data.notice, sizeof(data.notice), "test5 -> All");
    render("controls-row");
    data.select = sotiSelectColumn;
    data.notice[0] = '\0';
    render("controls-column");
    data.select = sotiSelectAll;
    data.statusFlags = 0; // paused
    render("controls-all");

    // a long list, scrolled, with long names, analysis and an error
    data.select = sotiSelectOne;
    data.statusFlags = sotiStatusPlaying | sotiStatusAnalyzing;
    data.focus = 0;
    files(40);
    snprintf(data.fileNames[22], sizeof(data.fileNames[22]), "very-long-file-name-of-a-place");
    data.fileFlags[22] = sotiFileA | sotiFileB | sotiFileC;
    data.fileFlags[23] = sotiFilePending;
    data.fileFlags[24] = sotiFileError;
    data.fileCursor = 22;
    data.fileTop = 6;
    render("files-scrolled");

    data.quitArmed = true;
    render("quit");

    data.quitArmed = false;
    files(0);
    data.fileTop = 0;
    data.fileCursor = 0;
    render("no-files");

    return ok ? 0 : 1;
}

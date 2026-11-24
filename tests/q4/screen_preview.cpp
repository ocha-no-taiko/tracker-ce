// Host-side preview of the Q-4 screen: runs the real drawing code of
// core/interface/q4/q4Screen.cpp against a stub display list, with the font
// metrics of Q4.ptfont. tests/dawControl/preview_screen.py draws it.
//   screen_preview font.ptfont Q4Control.json out_prefix
// writes out_prefix<shot>.txt for every shot and fails when a shot needs
// more display-list words than a frame allows.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "q4/q4Screen.h"

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

static Q4ScreenData data;

static void cell(int i, uint8_t kind, const char* label, float value = 0.0f, const char* text = "",
                 const char* mode = "", uint8_t flags = 0, float mod = 0.0f, uint8_t icon = 0)
{
    Q4ScreenData::Cell& c = data.cells[i];
    c.kind = kind;
    c.flags = flags;
    c.icon = icon;
    c.value = (uint16_t)lroundf(value * 16383.0f);
    c.mod = (int16_t)lroundf(mod * 8191.0f);
    snprintf(c.label, sizeof(c.label), "%s", label);
    snprintf(c.text, sizeof(c.text), "%s", text);
    snprintf(c.mode, sizeof(c.mode), "%s", mode);
}

static void page(uint8_t tab, const char* title, uint8_t pageIndex, uint8_t count, uint8_t panel)
{
    memset(data.cells, 0, sizeof(data.cells));
    data.tab = tab;
    data.pageValid = true;
    data.pageTab = tab;
    data.page = pageIndex;
    data.pageCount = count;
    data.panel = panel;
    data.pageFlags = 0;
    snprintf(data.pageTitle, sizeof(data.pageTitle), "%s", title);
}

static void fillWave()
{
    data.waveCount = Q4ScreenData::kWaveMax;
    for (int i = 0; i < Q4ScreenData::kWaveMax; i++)
    {
        float env = 0.15f + 0.85f * fabsf(sinf(i * 0.11f)) * expf(-(i % 43) / 30.0f);
        int amp = (int)(env * 60.0f);
        data.waveMax[i] = (uint8_t)(64 + amp);
        data.waveMin[i] = (uint8_t)(64 - amp * 9 / 10);
    }
    data.markStart = 0;
    data.markEnd = 16383;
    data.markLoop = Q4ScreenData::kNoMarker;
    data.playhead = 5200;
    data.sliceCount = 4;
    for (int i = 0; i < 4; i++)
        data.slices[i] = (uint16_t)(i * 4096);
}

enum
{
    U = q4CellUnipolar,
    B = q4CellBipolar,
    X = q4CellBox,
    A = q4CellAction,
};

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s font.ptfont Q4Control.json out_prefix\n", argv[0]);
        return 2;
    }
    if (!loadFonts(argv[1]))
    {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    static Q4Config cfg;
    q4ConfigSetDefaults(cfg);
    FILE* jf = fopen(argv[2], "rb");
    static char json[65536];
    size_t n = jf ? fread(json, 1, sizeof(json), jf) : 0;
    if (jf)
        fclose(jf);
    if (n > 0 && !q4ConfigParse(json, n, cfg))
    {
        fprintf(stderr, "JSON error\n");
        return 1;
    }
    data.cfg = &cfg;
    data.ownFont = true;
    q4ScreenReset(data);
    for (int i = 0; i < 8; i++)
        snprintf(data.labels[i], sizeof(data.labels[i]), "%s", cfg.tabLabels[i]);
    data.connected = true;
    data.bpmTenths = 1200;
    snprintf(data.sampleName, sizeof(data.sampleName), "test_120BPM");
    fillWave();

    bool ok = true;
    FILE* sink = fopen("/dev/null", "w");
    auto render = [&](const char* name) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s.txt", argv[3], name);
        g_list = fopen(path, "w");
        unsigned total = 0;
        for (int part = 0; part < q4ScreenPartCount; part++)
        {
            strControlProperties prop;
            prop.value = part;
            prop.data = &data;
            cQ4Screen screen(&prop);
            g_words = 0;
            screen.update();
            total += g_words;

            FILE* list = g_list;
            g_list = sink;
            const unsigned words = g_words;
            cQ4Screen poked(&prop);
            poked.setValue(q4ScreenHeader);
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

    // Material: DISC page 1 (the design's first screen)
    page(q4TabMaterial, "Material.Disc", 0, 2, q4PanelWave);
    cell(0, B, "SPEED", 0.5f, "x1", "SYNC", q4FlagModes);
    cell(1, X, "TEMPO", 0, "120 BPM", "SYNC", q4FlagModes);
    cell(2, X, "START", 0, "0:0", "1/1", q4FlagModes);
    cell(3, X, "LENGTH", 0, "4:0", "1/1", q4FlagModes);
    cell(6, B, "OFFSET", 0.5f, "0.00 s");
    cell(7, U, "XFADE", 0.0f, "0 %");
    cell(8, A, "LOAD");
    cell(9, A, "FREEZE");
    cell(10, A, "REVERSE", 0, "", "", q4FlagOn);
    cell(11, A, "RECORD");
    data.cursor = 0;
    render("material-disc");

    page(q4TabMaterial, "Material.Disc", 1, 2, q4PanelWave);
    cell(0, U, "GLIDE", 0.0f, "0 %");
    cell(1, U, "LEVEL", 0.8f, "0.0 dB");
    cell(2, U, "SOS", 0.0f, "0 %");
    cell(3, U, "DENSITY", 0.75f, "75 %");
    cell(7, U, "WET", 0.8f, "80 %");
    cell(8, A, "LOAD");
    cell(9, A, "FREEZE");
    cell(10, A, "REVERSE");
    cell(11, A, "RECORD");
    data.cursor = 1;
    render("material-disc2");

    page(q4TabMaterial, "Material.Tape", 0, 1, q4PanelWave);
    cell(0, B, "SPEED", 0.5f, "x1.00", "CHROMATIC", q4FlagModes);
    cell(2, B, "ROTATE", 0.5f, "0 %");
    cell(3, X, "LENGTH", 0, "VARIABLE", "FREE", q4FlagModes);
    cell(4, U, "LEVEL", 0.8f, "0.0 dB");
    cell(5, B, "SOS", 0.5f, "0 %");
    cell(7, U, "XFADE", 0.0f, "0 %");
    cell(8, A, "LOAD");
    cell(9, A, "REWIND");
    cell(10, A, "REVERSE");
    cell(11, A, "RECORD", 0, "", "", q4FlagOn);
    data.cursor = 5;
    render("material-tape");

    page(q4TabMaterial, "Material.Poly", 0, 2, q4PanelWave);
    cell(0, B, "PITCH", 0.5f, "0 st", "CHROMATIC", q4FlagModes);
    cell(2, U, "START", 0.0f, "0 %");
    cell(3, U, "LENGTH", 1.0f, "100 %");
    cell(4, U, "LEVEL", 0.8f, "0.0 dB");
    cell(6, U, "LOOP", 0.0f, "0 %");
    cell(7, U, "XFADE", 0.0f, "0 %");
    cell(8, A, "LOAD");
    cell(9, A, "LOOP", 0, "", "", 0, 0, q4IconLoopPingPong);
    cell(10, A, "OVERDUB");
    cell(11, A, "TRIG", 0, "", "", q4FlagNoteTrig);
    data.cursor = 9;
    render("material-poly");

    page(q4TabGranular, "Granular", 0, 2, q4PanelEmpty);
    cell(0, B, "PITCH", 0.5f, "0 st", "CHROMATIC", q4FlagModes);
    cell(1, U, "RATE", 0.5f, "1/16", "STRAIGHT", q4FlagModes);
    cell(2, U, "SIZE", 0.3f, "0.25 beat");
    cell(3, B, "CONTOUR", 0.5f, "0 %");
    cell(4, U, "SCAN", 0.2f, "x0.20", "STRETCH", q4FlagModes);
    cell(5, U, "SPRAY", 0.1f, "10 %", "RANDOM", q4FlagModes);
    cell(6, U, "PATTERN", 0.0f, "0 %");
    cell(7, U, "WET", 1.0f, "100 %", "MIX", q4FlagModes);
    cell(11, A, "LOCK");
    data.cursor = 4;
    render("granular");

    page(q4TabFilter, "Filter", 0, 2, q4PanelEmpty);
    cell(0, U, "CUTOFF", 0.75f, "2.40 kHz", "", q4FlagModulated, 0.3f);
    cell(1, U, "RESONANCE", 0.0f, "0 %");
    cell(2, U, "DECAY", 0.0f, "0 %", "SUSTAIN", q4FlagModes);
    cell(3, B, "PITCH", 0.5f, "0 st");
    cell(4, U, "SLOPE", 0.0f, "LP");
    cell(5, B, "TONE", 0.5f, "0 %");
    cell(6, X, "SCALE", 0, "CHROMATIC");
    cell(7, U, "WET", 0.8f, "80 %", "MIX", q4FlagModes);
    data.cursor = 0;
    data.slot = 0;
    render("filter");

    page(q4TabColor, "Color", 0, 1, q4PanelColor);
    cell(0, U, "DRIVE", 0.0f, "0 %");
    cell(1, U, "COMPRESS", 0.0f, "0 %");
    cell(2, U, "CRUSH", 0.0f, "0 %", "BIT CRUSH", q4FlagModes);
    cell(3, B, "TILT", 0.5f, "0 %", "MID", q4FlagModes);
    cell(4, U, "NOISE", 0.0f, "0 %", "PINK", q4FlagModes);
    cell(5, U, "NOISE DECAY", 0.75f, "75 %");
    cell(6, U, "NOISE TONE", 0.75f, "75 %");
    cell(7, U, "WET", 0.8f, "80 %", "MIX", q4FlagModes);
    data.cursor = 5;
    render("color");

    page(q4TabSpace, "Space", 0, 1, q4PanelSpace);
    cell(0, U, "DELAY", 0.0f, "0 %", "MIX", q4FlagModes);
    cell(1, U, "TIME", 0.75f, "1/8", "STRAIGHT", q4FlagModes);
    cell(2, U, "REVERB", 0.0f, "0 %", "MIX", q4FlagModes);
    cell(3, U, "SIZE", 0.75f, "75 %");
    cell(4, U, "FEEDBACK", 0.75f, "75 %");
    cell(5, U, "SPREAD", 0.0f, "0 %", "PING PONG", q4FlagModes);
    cell(6, B, "TONE", 0.5f, "OFF");
    cell(7, U, "DECAY", 0.75f, "4.0 s");
    cell(8, A, "CLEAR");
    cell(11, A, "FREEZE");
    data.cursor = 6;
    render("space");

    page(q4TabMod, "MOD1.Wave", 0, 1, q4PanelEmpty);
    cell(0, U, "RATE", 0.75f, "1/4", "STRAIGHT", q4FlagModes);
    cell(1, U, "AMOUNT", 0.75f, "75 %");
    cell(2, U, "PHASE", 0.0f, "0 deg");
    cell(3, B, "OFFSET", 0.5f, "0 %");
    cell(4, B, "SKEW", 0.5f, "0 %");
    cell(5, U, "FOLD", 0.0f, "0 %");
    cell(6, B, "CURVE", 0.5f, "0 %");
    cell(7, B, "SPREAD", 0.5f, "0 deg");
    cell(8, A, "SELECT");
    cell(9, A, "SIN");
    render("mod-wave");

    // mapping mode on the Filter page
    page(q4TabFilter, "Filter", 0, 2, q4PanelEmpty);
    data.pageFlags = 2;
    data.mapping = true;
    data.slot = 2;
    cell(0, U, "CUTOFF", 0.5f, "1.00 kHz", "", q4FlagModulated, -0.4f);
    cell(1, U, "RESONANCE", 0.2f, "20 %", "", q4FlagModulated, 0.2f);
    cell(2, U, "DECAY", 0.0f, "0 %", "SUSTAIN", q4FlagModes);
    cell(3, B, "PITCH", 0.5f, "0 st", "", q4FlagModulated, 0.1f);
    cell(4, U, "SLOPE", 0.0f, "LP");
    cell(5, B, "TONE", 0.5f, "0 %");
    cell(6, X, "SCALE", 0, "MAJOR");
    cell(7, U, "WET", 0.8f, "80 %", "MIX", q4FlagModes);
    data.cursor = 1;
    render("mapping");
    data.mapping = false;

    // bypassed slot
    page(q4TabColor, "Color", 0, 1, q4PanelColor);
    data.pageFlags = 1;
    cell(0, U, "DRIVE", 0.4f, "40 %");
    cell(7, U, "WET", 0.8f, "80 %");
    data.cursor = 0;
    render("bypass");

    data.tab = q4TabHelp;
    data.helpPage = q4HelpMaterial;
    data.helpScroll = 0;
    render("help-material");
    data.helpPage = q4HelpCommands;
    render("help-commands");
    data.helpPage = q4HelpCredit;
    render("help-credit");

    data.tab = q4TabQuit;
    render("quit");

    // not connected yet
    q4ScreenReset(data);
    data.tab = q4TabMaterial;
    render("waiting");

    printf("help lines: material %u (visible %u)\n", (unsigned)q4ScreenHelpLineCount(data),
           (unsigned)q4ScreenHelpVisibleLines());
    return ok ? 0 : 1;
}

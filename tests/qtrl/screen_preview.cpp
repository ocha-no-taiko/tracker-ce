// Host-side preview of the Q.T.R.L. screen: runs the real drawing code of
// core/interface/qtrl/qtrlScreen.cpp against a stub display list, with the
// font metrics of Qtrl.ptfont. tests/dawControl/preview_screen.py draws it.
//   screen_preview font.ptfont QtrlControl.json out_prefix
// writes out_prefix<tab>.txt for every tab (plus "status-alert") and fails
// when a tab needs more display-list words than a frame allows.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "qtrl/qtrlScreen.h"

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

static QtrlScreenData data;

static void setNode(int i, uint8_t state, const char* status)
{
    data.nodes[i].state = state;
    snprintf(data.nodes[i].status, sizeof(data.nodes[i].status), "%s", status);
}

static void fillSample(const QtrlConfig& cfg, bool alert)
{
    qtrlScreenReset(data);
    data.connected = true;
    for (int i = 0; i < 8; i++)
        snprintf(data.labels[i], sizeof(data.labels[i]), "%s", cfg.labels[i]);
    for (int i = 0; i < 5; i++)
        setNode(i, qtrlStateOk, "Operating Normally.");
    data.systemState = qtrlStateOk;
    snprintf(data.systemStatus, sizeof(data.systemStatus), "Operating Normally.");
    if (alert)
    {
        setNode(3, qtrlStateAnomaly, "Anomaly Detected.");
        data.systemState = qtrlStateAnomaly;
        snprintf(data.systemStatus, sizeof(data.systemStatus), "Anomaly Detected.");
        data.detailCount = 3;
        snprintf(data.detail[0], sizeof(data.detail[0]), "FAB1: print error 0x0300400C (FAILED)");
        snprintf(data.detail[1], sizeof(data.detail[1]), "Room: 24.5 C / 41 %%");
        snprintf(data.detail[2], sizeof(data.detail[2]), "A detail line that is far too long for the pane, cut off");
        snprintf(data.notice, sizeof(data.notice), "FAB1: Anomaly Detected.");
    }

    static const char* dates[] = { "2026.11.23", "2026.11.24", "2026.11.25", "2026.11.26", "2026.11.27",
                                   "2026.11.28", "2026.11.29", "2026.11.30", "2026.12.01", "2026.12.02" };
    data.dateCount = 10;
    for (int i = 0; i < 10; i++)
        snprintf(data.dates[i], sizeof(data.dates[i]), "%s", dates[i]);
    data.selectedDate = 0;
    data.dateTopRow = 0;
    snprintf(data.logDate, sizeof(data.logDate), "2026.11.23");
    static const char* lines[][2] = {
        { "00:00:00", "Login" }, { "11:11:11", "Printed Model (test.stl)" },
        { "12:00:03", "QTRL2: Snapshot saved (2026-11-23_120003.jpg)" }, { "13:20:00", "FAB1: Print started (benchy)" },
    };
    data.logCount = 40;
    for (int i = 0; i < 40; i++)
    {
        snprintf(data.logTime[i], sizeof(data.logTime[i]), "%s", lines[i % 4][0]);
        snprintf(data.logText[i], sizeof(data.logText[i]), "%s", lines[i % 4][1]);
    }

    qtrlScreenChatAdd(data, "user", "Hello qoala.");
    qtrlScreenChatAdd(data, "qoala", "Hi! Feel free to ask me anything :)");
    qtrlScreenChatAdd(data, "user", "How is the printer doing?\nAnd the room?");
    qtrlScreenChatAdd(data, "qoala",
                      "FAB1 is printing benchy at 42%. The room is 24.5 C, which is comfortable for PLA. "
                      "I will tell you when the print finishes.");
    data.chatBusy = alert;
    snprintf(data.input, sizeof(data.input), "Thank you qoala.");
    data.inputLength = (uint16_t)strlen(data.input);
    data.pressedKey = alert ? 27 : -1;
    data.caps = alert;
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s font.ptfont QtrlControl.json out_prefix\n", argv[0]);
        return 2;
    }
    if (!loadFonts(argv[1]))
    {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    static QtrlConfig cfg;
    qtrlConfigSetDefaults(cfg);
    FILE* jf = fopen(argv[2], "rb");
    static char json[16384];
    size_t n = jf ? fread(json, 1, sizeof(json), jf) : 0;
    if (jf)
        fclose(jf);
    if (n > 0 && !qtrlConfigParse(json, n, cfg))
    {
        fprintf(stderr, "JSON error\n");
        return 1;
    }
    data.cfg = &cfg;
    data.ownFont = true;

    struct Shot
    {
        const char* name;
        uint8_t tab;
        bool alert;
    };
    static const Shot shots[] = {
        { "home", qtrlTabHome, false },    { "status", qtrlTabStatus, false }, { "status-alert", qtrlTabStatus, true },
        { "logs", qtrlTabLogs, false },    { "chat", qtrlTabChat, false },     { "chat-busy", qtrlTabChat, true },
        { "help", qtrlTabHelp, false },    { "quit", qtrlTabQuit, false },
    };
    bool ok = true;
    for (const Shot& shot : shots)
    {
        fillSample(cfg, shot.alert);
        data.tab = shot.tab;
        char path[512];
        snprintf(path, sizeof(path), "%s%s.txt", argv[3], shot.name);
        g_list = fopen(path, "w");
        unsigned total = 0;
        for (int part = 0; part < qtrlScreenPartCount; part++)
        {
            strControlProperties prop;
            prop.value = part;
            prop.data = &data;
            cQtrlScreen screen(&prop);
            g_words = 0;
            screen.update();
            total += g_words;
        }
        fclose(g_list);
        printf("%-13s %4u words (a frame allows 2043)\n", shot.name, total);
        if (total > 2043)
            ok = false;
    }
    return ok ? 0 : 1;
}

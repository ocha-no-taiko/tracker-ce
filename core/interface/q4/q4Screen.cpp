#include "q4/q4Screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "FT812.h"
#include "displayStructs.h"

// Layout of the 800 x 480 screen, measured on the design (Q-4_UI_v1, 640 x
// 360 units, x * 1.25, y * 1.33). y values of text are the vertical middle of
// the line. The text helpers are the same as the Q.T.R.L. screen's.
namespace
{
// Font slots of Q4.ptfont (tools/fonts/make_ptfont.py --layout qtrl)
enum FontRole : uint8_t
{
    fontMain = 0,  // 8 px per character
    fontSmall = 1, // 6 px
    fontTiny = 2,  // 5 px
    fontBold = 3,
};
// The UI font slots used when the screen font could not be loaded.
constexpr uint8_t kFallbackFont[4] = { 0, 4, 4, 2 };
constexpr uint8_t kCopyrightCell = 127;

constexpr int16_t kScreenW = 800;
constexpr int16_t kScreenH = 480;

// title bar
constexpr int16_t kTitleX = 19;
constexpr int16_t kTitleY = 13;
constexpr int16_t kHeadingX = 102;
constexpr int16_t kNoticeRight = 790;
constexpr int16_t kTitleLineY = 27;

// divider and right pane
constexpr int16_t kDividerX = 399;
constexpr int16_t kDividerTop = 42;
constexpr int16_t kDividerBottom = 450;
constexpr int16_t kDashOn = 6;
constexpr int16_t kDashOff = 4;
constexpr int16_t kPaneTitleX = 414;
constexpr int16_t kPaneTitleY = 66;
constexpr int16_t kPaneRight = 775;

// left pane of the device tabs
constexpr int16_t kInfoY = 80;
constexpr int16_t kBoxX0 = 26;
constexpr int16_t kBoxY0 = 94;
constexpr int16_t kBoxX1 = 376;
constexpr int16_t kBoxY1 = 385;

// parameter grid
constexpr int16_t kGridX = 426;
constexpr int16_t kGridY = 94;
constexpr int16_t kCellW = 87;
constexpr int16_t kParamH = 46;
constexpr int16_t kActionH = 34;
constexpr int16_t kGaugeInset = 6;
constexpr int16_t kGaugeTop = 24;
constexpr int16_t kGaugeH = 11;
constexpr int16_t kInfoLineY = 246;
constexpr int16_t kInfoLine2Y = 268;

// waveform
constexpr int16_t kWaveX = 30;
constexpr int16_t kWaveStep = 2;
constexpr int16_t kWaveMid = 250;
constexpr int16_t kWaveHalf = 118;

// help
constexpr int16_t kFileX[3] = { 20, 131, 243 };
constexpr int16_t kFileY[3] = { 53, 172, 291 };
constexpr int16_t kFileW = 100;
constexpr int16_t kFileH = 107;
constexpr int16_t kFold = 25;
constexpr int16_t kHintY = 425;
constexpr int16_t kLeftHintCenter = 199;
constexpr int16_t kRightHintCenter = 590;
constexpr int16_t kHelpX = 426;
constexpr int16_t kHelpFirstY = 105;
constexpr int16_t kHelpStep = 22;
constexpr int16_t kHelpRight = 762;
constexpr int16_t kScrollTop = 53;
constexpr int16_t kScrollBottom = 410;
constexpr int16_t kScrollW = 13;
constexpr int16_t kScrollX = 773;
constexpr uint8_t kHelpRows = (kScrollBottom - kHelpFirstY) / kHelpStep + 1;

// credit
constexpr int16_t kLogoCell = 3;
constexpr int16_t kCreditCenter = 590;
constexpr int16_t kLogoY = 92;
constexpr int16_t kCreditTaglineY = 165;
constexpr int16_t kCreditFirstY = 214;
constexpr int16_t kCreditStep = 24;
constexpr int16_t kCreditIndent = 32;

// QUIT
constexpr int16_t kQuitY = 237;

// bottom labels
constexpr int16_t kLabelsY = 464;
constexpr int16_t kLabelStep = kScreenW / 8;

// Logo of the Credit page, one character per pixel cell.
constexpr const char* kLogo[20] = {
    "....############........####....####", "....############........####....####",
    "....############........####....####", "....############........####....####",
    "####............####....####....####", "####............####....####....####",
    "####............####....####....####", "####............####....####....####",
    "####............####....############", "####......##....####....############",
    "####.....####...####....############", "####.....#####..####....############",
    "####......#####.####............####", "####.......#########............####",
    "####........########............####", "####.........#######............####",
    "....################............####", "....################............####",
    "....################............####", "....################............####",
};

const Q4ScreenData* s_data = nullptr;

const strFont& fontFor(uint8_t role)
{
    bool own = (s_data != nullptr && s_data->ownFont);
    return fonts[own ? role : kFallbackFont[role]];
}

// Next character cell of a UTF-8 string: ASCII as is, the copyright sign as
// cell 127, anything else as '?'. Advances *text.
uint8_t nextCell(const char*& text)
{
    uint8_t c = (uint8_t)*text++;
    if (c < 0x80)
        return c;
    if (c == 0xC2 && (uint8_t)*text == 0xA9)
    {
        text++;
        return kCopyrightCell;
    }
    while (((uint8_t)*text & 0xC0) == 0x80)
        text++;
    return '?';
}

uint8_t cellWidth(const strFont& font, uint8_t& cell)
{
    if (cell >= 128 || font.data[cell] == 0)
        cell = '?';
    return font.data[cell];
}

int16_t textWidth(uint8_t role, const char* text, uint16_t length = 0xFFFF)
{
    const strFont& font = fontFor(role);
    const char* end = text + length;
    int16_t w = 0;
    while (*text && text < end)
    {
        uint8_t cell = nextCell(text);
        w += cellWidth(font, cell);
    }
    return w;
}

// Draws one line (up to length bytes) with its middle on y; stops before
// maxX. Returns the x after the last character drawn.
int16_t drawText(int16_t x, int16_t y, uint8_t role, const char* text, int16_t maxX,
                 uint16_t length = 0xFFFF)
{
    const strFont& font = fontFor(role);
    const int16_t top = y - font.height / 2;
    const char* end = text + length;

    API_BEGIN(BITMAPS);
    API_BITMAP_HANDLE(font.handle);
    while (*text && text < end)
    {
        uint8_t cell = nextCell(text);
        uint8_t w = cellWidth(font, cell);
        if (x + w > maxX)
            break;
        if (cell != ' ' && x >= 0 && top >= 0)
        {
            if (x <= 511 && top <= 511)
            {
                API_VERTEX2II(x, top, font.handle, cell);
            }
            else
            {
                API_CELL(cell);
                API_VERTEX2F(x, top);
            }
        }
        x += w;
    }
    API_END();
    return x;
}

void drawCentered(int16_t centerX, int16_t y, uint8_t role, const char* text)
{
    drawText(centerX - textWidth(role, text) / 2, y, role, text, kScreenW);
}

// Like drawText, but a text too long for the room ends in "...".
void drawFitted(int16_t x, int16_t y, uint8_t role, const char* text, int16_t maxX)
{
    if (x + textWidth(role, text) <= maxX)
    {
        drawText(x, y, role, text, maxX);
        return;
    }
    const int16_t room = maxX - x - textWidth(role, "...");
    const char* end = text;
    const char* p = text;
    while (*p)
    {
        nextCell(p);
        if (textWidth(role, text, (uint16_t)(p - text)) > room)
            break;
        end = p;
    }
    x = drawText(x, y, role, text, maxX, (uint16_t)(end - text));
    drawText(x, y, role, "...", maxX);
}

// The main font when the text fits in room, the small one otherwise.
uint8_t roleToFit(const char* text, int16_t room)
{
    return textWidth(fontMain, text) <= room ? fontMain : fontSmall;
}

// Length (bytes) of the longest run of whole words of text that fits in
// room pixels; a single word longer than the room is cut.
uint16_t fitWords(uint8_t role, const char* text, int16_t room)
{
    const char* lineEnd = text;
    const char* p = text;
    while (*p && *p != '\n')
    {
        const char* wordEnd = p;
        while (*wordEnd == ' ')
            wordEnd++;
        while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n')
            wordEnd++;
        if (textWidth(role, text, (uint16_t)(wordEnd - text)) > room)
            break;
        lineEnd = wordEnd;
        p = wordEnd;
    }
    if (lineEnd == text && *text && *text != '\n')
    {
        const char* q = text;
        while (*q && *q != '\n')
        {
            const char* next = q;
            nextCell(next);
            if (textWidth(role, text, (uint16_t)(next - text)) > room)
                break;
            q = next;
        }
        lineEnd = (q == text) ? text + 1 : q;
    }
    return (uint16_t)(lineEnd - text);
}

void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y1);
}

// Outline of a rectangle; call between API_BEGIN(LINES) and API_END().
void outline(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    line(x0, y0, x1, y0);
    line(x1, y0, x1, y1);
    line(x1, y1, x0, y1);
    line(x0, y1, x0, y0);
}

void beginLines()
{
    API_LINE_WIDTH(8); // 1 px
    API_BEGIN(LINES);
}

void endLines()
{
    API_END();
    API_LINE_WIDTH(16);
}

void fillRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    API_BEGIN(RECTS);
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y1);
    API_END();
}

// Pixel art: '#' cells filled, cell x cell pixels each.
void drawPixels(const char* const* rows, uint8_t rowCount, int16_t x, int16_t y, int16_t cell)
{
    API_BEGIN(RECTS);
    for (uint8_t r = 0; r < rowCount; r++)
    {
        const char* row = rows[r];
        for (uint8_t c = 0; row[c]; c++)
        {
            if (row[c] != '#')
                continue;
            uint8_t end = c;
            while (row[end + 1] == '#')
                end++;
            API_VERTEX2F(x + c * cell, y + r * cell);
            API_VERTEX2F(x + (end + 1) * cell - 1, y + (r + 1) * cell - 1);
            c = end;
        }
    }
    API_END();
}

void drawDashedVertical(int16_t x, int16_t top, int16_t bottom, int16_t on, int16_t off)
{
    for (int16_t y = top; y < bottom; y += on + off)
    {
        int16_t end = y + on;
        if (end > bottom)
            end = bottom;
        line(x, y, x, end);
    }
}

// Circle as a closed line strip of `segments` pieces.
void circle(float cx, float cy, float r, uint8_t segments)
{
    API_BEGIN(LINE_STRIP);
    for (uint8_t i = 0; i <= segments; i++)
    {
        float a = 6.2831853f * i / segments;
        API_VERTEX2F((int16_t)lroundf(cx + r * cosf(a)), (int16_t)lroundf(cy + r * sinf(a)));
    }
    API_END();
}

// x range where the line x - y = c crosses a circle; false if it misses.
bool chordX(float cx, float cy, float r, float c, float& x0, float& x1)
{
    // points (x, x - c): (x - cx)^2 + (x - c - cy)^2 = r^2
    const float d = c + cy;
    const float a = 2.0f;
    const float b = -2.0f * cx - 2.0f * d;
    const float k = cx * cx + d * d - r * r;
    const float disc = b * b - 4.0f * a * k;
    if (disc <= 0.0f)
        return false;
    const float s = sqrtf(disc);
    x0 = (-b - s) / (2.0f * a);
    x1 = (-b + s) / (2.0f * a);
    return true;
}

void drawScrollBar(int16_t x, uint16_t total, uint16_t first, uint16_t shown, uint32_t lineColor)
{
    API_COLOR(lineColor);
    beginLines();
    outline(x, kScrollTop, x + kScrollW, kScrollBottom);
    endLines();
    const int16_t room = kScrollBottom - kScrollTop - 2;
    int16_t y0 = kScrollTop + 1;
    int16_t y1 = kScrollBottom - 1;
    if (total > shown && total > 0)
    {
        y0 = kScrollTop + 1 + (int16_t)((int32_t)room * first / total);
        y1 = kScrollTop + 1 + (int16_t)((int32_t)room * (first + shown) / total);
        if (y1 < y0 + 4)
            y1 = y0 + 4;
    }
    fillRect(x + 1, y0, x + kScrollW - 1, y1);
}

// Sheet with a folded corner (help tiles).
void drawSheet(int16_t x, int16_t y)
{
    beginLines();
    line(x, y, x + kFileW - kFold, y);
    line(x + kFileW - kFold, y, x + kFileW, y + kFold);
    line(x + kFileW, y + kFold, x + kFileW, y + kFileH);
    line(x + kFileW, y + kFileH, x, y + kFileH);
    line(x, y + kFileH, x, y);
    line(x + kFileW - kFold, y, x + kFileW - kFold, y + kFold);
    line(x + kFileW - kFold, y + kFold, x + kFileW, y + kFold);
    endLines();
}

// An arrow from x0 to x1 at y (head at x1).
void arrow(int16_t x0, int16_t x1, int16_t y)
{
    const int16_t head = x1 > x0 ? -4 : 4;
    line(x0, y, x1, y);
    line(x1, y, x1 + head, y - 4);
    line(x1, y, x1 + head, y + 4);
}

bool isDeviceTab(uint8_t tab)
{
    return tab <= q4TabMod;
}

// One help line with its wrapping: calls fn(text, length, isHeading) for
// every wrapped line of the current page.
template <typename Fn>
void forEachHelpLine(const Q4ScreenData& data, Fn fn)
{
    const Q4Config& cfg = *data.cfg;
    if (data.helpPage >= q4HelpCredit)
        return;
    const int16_t room = kHelpRight - kHelpX;
    for (uint8_t i = 0; i < Q4Config::kHelpLines; i++)
    {
        const char* text = cfg.help[data.helpPage][i];
        // an empty line ends the text only when everything after it is empty
        bool more = false;
        for (uint8_t j = i; j < Q4Config::kHelpLines && !more; j++)
            more = cfg.help[data.helpPage][j][0] != '\0';
        if (!more)
            break;
        if (text[0] == '\0')
        {
            fn("", 0, false);
            continue;
        }
        const bool heading = text[0] == '#';
        while (*text)
        {
            while (*text == ' ')
                text++;
            if (*text == '\0')
                break;
            uint16_t n = fitWords(fontMain, text, room);
            if (!fn(text, n, heading))
                return;
            text += n;
        }
    }
}
} // namespace

//------------------------------------------------------------------------------
void q4ScreenReset(Q4ScreenData& data)
{
    data.connected = false;
    data.notice[0] = '\0';
    data.pageValid = false;
    data.pageTab = 0;
    data.track = 0;
    data.page = 0;
    data.pageCount = 1;
    data.slot = 0;
    data.pageFlags = 0;
    data.panel = q4PanelEmpty;
    data.pageTitle[0] = '\0';
    memset(data.cells, 0, sizeof(data.cells));
    data.mapping = false;
    data.scene = 0;
    data.bpmTenths = 1200;
    data.statusFlags = 0;
    data.sampleName[0] = '\0';
    data.waveCount = 0;
    data.markStart = Q4ScreenData::kNoMarker;
    data.markEnd = Q4ScreenData::kNoMarker;
    data.markLoop = Q4ScreenData::kNoMarker;
    data.playhead = Q4ScreenData::kNoMarker;
    data.sliceCount = 0;
}

uint16_t q4ScreenHelpLineCount(const Q4ScreenData& data)
{
    s_data = &data;
    uint16_t count = 0;
    forEachHelpLine(data, [&](const char*, uint16_t, bool) {
        count++;
        return true;
    });
    return count;
}

uint8_t q4ScreenHelpVisibleLines()
{
    return kHelpRows;
}

//------------------------------------------------------------------------------
cQ4Screen::cQ4Screen(strControlProperties* properties)
  : cDisplayControl(controlIdQ4Screen, 1)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    style = properties->style;
    value = properties->value;
    data = (const Q4ScreenData*)properties->data;
}

void cQ4Screen::update()
{
    if (data == nullptr || data->cfg == nullptr)
        return;
    s_data = data;

    switch (value)
    {
        case q4ScreenBackground: drawBackground(); break;
        case q4ScreenHeader: drawHeader(); break;
        case q4ScreenLeft: drawLeft(); break;
        case q4ScreenRight: drawRight(); break;
        case q4ScreenLabels: drawLabels(); break;
        default: break;
    }
}

void cQ4Screen::drawBackground()
{
    const Q4Config& cfg = *data->cfg;

    API_COLOR(cfg.backgroundColor);
    fillRect(0, 0, kScreenW - 1, kScreenH - 1);

    API_COLOR(cfg.lineColor);
    beginLines();
    outline(1, 1, kScreenW - 2, kScreenH - 2);
    line(1, kTitleLineY, kScreenW - 2, kTitleLineY);
    if (data->tab != q4TabQuit)
        drawDashedVertical(kDividerX, kDividerTop, kDividerBottom, kDashOn, kDashOff);
    endLines();

    API_COLOR(cfg.textColor);
    drawText(kTitleX, kTitleY, fontMain, cfg.title, kHeadingX - 4);
}

void cQ4Screen::drawHeader()
{
    const Q4Config& cfg = *data->cfg;
    char heading[24];
    snprintf(heading, sizeof(heading), "> %s", cfg.tabLabels[data->tab < q4TabCount ? data->tab : 0]);
    API_COLOR(cfg.textColor);
    int16_t x = drawText(kHeadingX, kTitleY, fontMain, heading, kScreenW - 10);

    char mapping[32];
    const char* notice = data->notice;
    uint32_t color = cfg.subTextColor;
    if (!data->connected && isDeviceTab(data->tab))
    {
        notice = cfg.waiting;
        color = cfg.dimColor;
    }
    else if (data->mapping)
    {
        snprintf(mapping, sizeof(mapping), cfg.mapping, data->slot + 1);
        notice = mapping;
        color = cfg.modColor;
    }
    if (notice[0] != '\0')
    {
        API_COLOR(color);
        int16_t w = textWidth(fontSmall, notice);
        int16_t nx = kNoticeRight - w;
        if (nx < x + 16)
            nx = x + 16;
        drawFitted(nx, kTitleY, fontSmall, notice, kNoticeRight);
    }
}

void cQ4Screen::drawLeft()
{
    if (isDeviceTab(data->tab))
        drawDeviceLeft();
    else if (data->tab == q4TabHelp)
        drawHelpLeft();
    else if (data->tab == q4TabQuit)
        drawQuit();
}

void cQ4Screen::drawRight()
{
    if (isDeviceTab(data->tab))
        drawDeviceRight();
    else if (data->tab == q4TabHelp)
        drawHelpRight();
}

void cQ4Screen::drawLabels()
{
    const Q4Config& cfg = *data->cfg;

    if (data->tab == q4TabQuit)
    {
        // only "Cancel" (button 7) and "QUIT" (button 8)
        API_COLOR(cfg.textColor);
        drawCentered(kLabelStep * 6 + kLabelStep / 2, kLabelsY, fontMain, cfg.cancel);
        API_COLOR(cfg.quitColor);
        drawCentered(kLabelStep * 7 + kLabelStep / 2, kLabelsY, fontMain, data->labels[7]);
        return;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        uint32_t color = cfg.textColor;
        if (i == q4TabQuit)
            color = cfg.quitColor;
        else if (i == data->tab)
            color = cfg.selectColor;
        API_COLOR(color);
        const char* text = data->labels[i];
        int16_t center = kLabelStep * i + kLabelStep / 2;
        drawText(center - textWidth(fontMain, text) / 2, kLabelsY, fontMain, text, kLabelStep * (i + 1));
    }
}

//------------------------------------------------------------------------------
// Device tabs (Material .. MOD)
void cQ4Screen::drawDeviceLeft()
{
    const Q4Config& cfg = *data->cfg;
    const bool ours = data->pageValid && data->pageTab == data->tab;

    char info[24];
    snprintf(info, sizeof(info), "[%u] %03u", data->track + 1, data->scene + 1);
    API_COLOR(cfg.textColor);
    drawText(kBoxX0 + 2, kInfoY, fontMain, info, kBoxX1);
    snprintf(info, sizeof(info), "BPM:%u", (data->bpmTenths + 5) / 10);
    drawText(kBoxX1 - textWidth(fontMain, info), kInfoY, fontMain, info, kBoxX1 + 1);

    API_COLOR(cfg.lineColor);
    beginLines();
    outline(kBoxX0, kBoxY0, kBoxX1, kBoxY1);
    endLines();

    if (!ours)
        return;
    switch (data->panel)
    {
        case q4PanelWave: drawWave(); break;
        case q4PanelColor: drawColorPanel(); break;
        case q4PanelSpace: drawSpacePanel(); break;
        default: break;
    }
}

void cQ4Screen::drawWave()
{
    const Q4Config& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    drawFitted(kBoxX0 + 4, kBoxY0 + 12, fontMain, data->sampleName, kBoxX1 - 4);

    const uint16_t count = data->waveCount < Q4ScreenData::kWaveMax ? data->waveCount : Q4ScreenData::kWaveMax;
    const int16_t width = Q4ScreenData::kWaveMax * kWaveStep;
    auto markerX = [&](uint16_t pos) { return (int16_t)(kWaveX + (int32_t)width * pos / 16383); };

    // dim the part outside start .. end
    int16_t startX = data->markStart != Q4ScreenData::kNoMarker ? markerX(data->markStart) : kWaveX;
    int16_t endX = data->markEnd != Q4ScreenData::kNoMarker ? markerX(data->markEnd) : kWaveX + width;

    if (count > 0)
    {
        beginLines();
        uint32_t current = 0;
        for (uint16_t i = 0; i < count; i++)
        {
            const int16_t x = kWaveX + i * kWaveStep;
            const uint32_t color = (x < startX || x > endX) ? cfg.dimColor : cfg.textColor;
            if (color != current)
            {
                API_COLOR(color);
                current = color;
            }
            int16_t y0 = kWaveMid - (int16_t)((data->waveMax[i] - 64) * kWaveHalf / 63);
            int16_t y1 = kWaveMid - (int16_t)((data->waveMin[i] - 64) * kWaveHalf / 63);
            if (y1 <= y0)
                y1 = y0 + 1;
            line(x, y0, x, y1);
        }
        endLines();
    }
    else
    {
        API_COLOR(cfg.dimColor);
        beginLines();
        line(kWaveX, kWaveMid, kWaveX + width, kWaveMid);
        endLines();
    }

    const int16_t top = kWaveMid - kWaveHalf - 4;
    const int16_t bottom = kWaveMid + kWaveHalf + 4;
    API_COLOR(cfg.selectColor);
    beginLines();
    if (data->markStart != Q4ScreenData::kNoMarker)
        line(startX, top, startX, bottom);
    if (data->markEnd != Q4ScreenData::kNoMarker)
        line(endX, top, endX, bottom);
    endLines();
    if (data->markLoop != Q4ScreenData::kNoMarker)
    {
        beginLines();
        drawDashedVertical(markerX(data->markLoop), top, bottom, 4, 4);
        endLines();
    }
    if (data->sliceCount > 0)
    {
        API_COLOR(cfg.subTextColor);
        beginLines();
        for (uint8_t i = 0; i < data->sliceCount && i < Q4ScreenData::kMaxSlices; i++)
        {
            int16_t x = markerX(data->slices[i]);
            line(x, bottom - 8, x, bottom);
        }
        endLines();
    }
    if (data->playhead != Q4ScreenData::kNoMarker)
    {
        API_COLOR(cfg.modColor);
        beginLines();
        int16_t x = markerX(data->playhead);
        line(x, top, x, bottom);
        endLines();
    }
}

void cQ4Screen::drawColorPanel()
{
    const Q4Config& cfg = *data->cfg;
    constexpr float kCy = 246.0f;
    constexpr float kR = 100.0f;
    constexpr float kCx[2] = { 152.0f, 250.0f };
    constexpr float kStep = 42.0f;

    API_COLOR(cfg.lineColor);
    API_LINE_WIDTH(8);
    circle(kCx[0], kCy, kR, 48);
    circle(kCx[1], kCy, kR, 48);

    // hatch: lines x - y = c through the union of both circles
    API_BEGIN(LINES);
    for (float c = -360.0f; c <= 260.0f; c += kStep)
    {
        float a0, a1, b0, b1;
        bool a = chordX(kCx[0], kCy, kR, c, a0, a1);
        bool b = chordX(kCx[1], kCy, kR, c, b0, b1);
        if (a && b && b0 <= a1)
        {
            a1 = b1 > a1 ? b1 : a1;
            b = false;
        }
        if (a)
            line((int16_t)a0, (int16_t)(a0 - c), (int16_t)a1, (int16_t)(a1 - c));
        if (b)
            line((int16_t)b0, (int16_t)(b0 - c), (int16_t)b1, (int16_t)(b1 - c));
    }
    endLines();
}

void cQ4Screen::drawSpacePanel()
{
    const Q4Config& cfg = *data->cfg;
    API_COLOR(cfg.lineColor);
    API_LINE_WIDTH(8);
    circle(113.0f, 238.0f, 75.0f, 48);

    // a box seen from a corner, with a second, lower lid inside it
    constexpr int16_t kTopY = 159, kUpperY = 195, kFrontY = 231, kLowerY = 273, kBottomY = 309;
    constexpr int16_t kLidBackY = 196, kLidY = 232, kLidFrontY = 268;
    constexpr int16_t kLeftX = 213, kMidX = 288, kRightX = 363;
    API_BEGIN(LINES);
    // outline
    line(kMidX, kTopY, kRightX, kUpperY);
    line(kRightX, kUpperY, kRightX, kLowerY);
    line(kRightX, kLowerY, kMidX, kBottomY);
    line(kMidX, kBottomY, kLeftX, kLowerY);
    line(kLeftX, kLowerY, kLeftX, kUpperY);
    line(kLeftX, kUpperY, kMidX, kTopY);
    // front edges of the top and the front corner
    line(kLeftX, kUpperY, kMidX, kFrontY);
    line(kMidX, kFrontY, kRightX, kUpperY);
    line(kMidX, kFrontY, kMidX, kBottomY);
    // the inner lid
    line(kLeftX, kLidY, kMidX, kLidBackY);
    line(kMidX, kLidBackY, kRightX, kLidY);
    line(kLeftX, kLidY, kMidX, kLidFrontY);
    line(kMidX, kLidFrontY, kRightX, kLidY);
    endLines();
}

void cQ4Screen::drawDeviceRight()
{
    const Q4Config& cfg = *data->cfg;
    const bool ours = data->pageValid && data->pageTab == data->tab;
    const bool bypassed = ours && (data->pageFlags & 1);

    char heading[32];
    snprintf(heading, sizeof(heading), "> %s:", ours ? data->pageTitle : cfg.tabLabels[data->tab]);
    API_COLOR(cfg.textColor);
    int16_t x = drawText(kPaneTitleX, kPaneTitleY, fontMain, heading, kPaneRight - 40);

    if (ours && bypassed)
    {
        API_COLOR(cfg.quitColor);
        drawText(x + 10, kPaneTitleY, fontSmall, cfg.bypassed, kPaneRight - 40);
    }
    if (ours && data->pageCount > 1)
    {
        char pages[8];
        snprintf(pages, sizeof(pages), "%u/%u", data->page + 1, data->pageCount);
        API_COLOR(cfg.subTextColor);
        drawText(kPaneRight - textWidth(fontSmall, pages), kPaneTitleY, fontSmall, pages, kPaneRight + 1);
    }

    // grid
    const int16_t x0 = kGridX;
    const int16_t x1 = kGridX + kCellW * Q4ScreenData::kColumns;
    const int16_t yRow1 = kGridY + kParamH;
    const int16_t yRow2 = kGridY + 2 * kParamH;
    const int16_t y1 = yRow2 + kActionH;
    API_COLOR(bypassed ? cfg.dimColor : cfg.lineColor);
    beginLines();
    outline(x0, kGridY, x1, y1);
    line(x0, yRow1, x1, yRow1);
    line(x0, yRow2, x1, yRow2);
    for (uint8_t c = 1; c < Q4ScreenData::kColumns; c++)
        line(x0 + c * kCellW, kGridY, x0 + c * kCellW, y1);
    endLines();

    if (!ours)
        return;

    for (uint8_t i = 0; i < Q4ScreenData::kCells; i++)
    {
        const uint8_t row = i / Q4ScreenData::kColumns;
        const uint8_t col = i % Q4ScreenData::kColumns;
        const int16_t y = kGridY + row * kParamH;
        drawCell(i, x0 + col * kCellW, y, kCellW, row < 2 ? kParamH : kActionH);
    }

    // the selected cell in words
    const Q4ScreenData::Cell& sel = data->cells[data->cursor < Q4ScreenData::kCells ? data->cursor : 0];
    if (sel.kind != q4CellEmpty && sel.kind != q4CellAction)
    {
        char textLine[64];
        snprintf(textLine, sizeof(textLine), "> %s: %s", sel.label, sel.text);
        API_COLOR(cfg.textColor);
        int16_t tx = drawText(kGridX, kInfoLineY, fontMain, textLine, kPaneRight);
        if (sel.mode[0] != '\0')
        {
            snprintf(textLine, sizeof(textLine), "[%s]", sel.mode);
            API_COLOR(cfg.subTextColor);
            drawFitted(tx + 12, kInfoLineY, fontSmall, textLine, kPaneRight);
        }
        if (sel.flags & q4FlagModulated)
        {
            int16_t percent = (int16_t)((int32_t)sel.mod * 100 / 8192);
            snprintf(textLine, sizeof(textLine), "MOD%u: %+d%%", data->slot + 1, percent);
            API_COLOR(cfg.modColor);
            drawText(kGridX, kInfoLine2Y, fontSmall, textLine, kPaneRight);
        }
    }
}

void cQ4Screen::drawCell(uint8_t index, int16_t x, int16_t y, int16_t w, int16_t h)
{
    const Q4Config& cfg = *data->cfg;
    const Q4ScreenData::Cell& cell = data->cells[index];
    const bool selected = index == data->cursor;
    const bool bypassed = data->pageFlags & 1;
    const bool dim = bypassed || (cell.flags & q4FlagDisabled);
    const uint32_t ink = dim ? cfg.dimColor : cfg.textColor;
    const uint32_t frame = dim ? cfg.dimColor : cfg.lineColor;

    if (selected)
    {
        API_COLOR(cfg.selectColor);
        beginLines();
        outline(x + 2, y + 2, x + w - 2, y + h - 2);
        endLines();
    }

    if (cell.kind == q4CellEmpty)
        return;

    if (cell.kind == q4CellAction)
    {
        const bool on = cell.flags & q4FlagOn;
        if (on)
        {
            API_COLOR(frame);
            fillRect(x + 4, y + 4, x + w - 4, y + h - 4);
        }
        const uint32_t textColor = on ? cfg.backgroundColor : (selected ? cfg.selectColor : ink);
        API_COLOR(textColor);
        const int16_t cx = x + w / 2;
        const int16_t cy = y + h / 2;
        if (cell.icon != q4IconNone)
        {
            beginLines();
            switch (cell.icon)
            {
                case q4IconLoopOff: arrow(cx - 12, cx + 12, cy); break;
                case q4IconLoopForward:
                    arrow(cx - 12, cx + 12, cy - 5);
                    arrow(cx - 12, cx + 12, cy + 5);
                    break;
                case q4IconLoopPingPong:
                    arrow(cx - 12, cx + 12, cy - 5);
                    arrow(cx + 12, cx - 12, cy + 5);
                    break;
                default: break;
            }
            endLines();
        }
        else
        {
            uint8_t role = roleToFit(cell.label, w - 6);
            drawCentered(cx, cy, role, cell.label);
        }
        return;
    }

    // parameter: label, then the gauge
    API_COLOR(selected ? cfg.selectColor : ink);
    uint8_t labelRole = roleToFit(cell.label, w - 6);
    drawFitted(x + 3, y + 10, labelRole, cell.label, x + w - 3);

    if (cell.flags & q4FlagModes)
    {
        // corner mark: the parameter has modes (Insert)
        API_LINE_WIDTH(8);
        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(x + w - 9, y + 4);
        API_VERTEX2F(x + w - 4, y + 4);
        API_VERTEX2F(x + w - 4, y + 9);
        API_VERTEX2F(x + w - 9, y + 4);
        API_END();
        API_LINE_WIDTH(16);
    }

    const int16_t gx0 = x + kGaugeInset;
    const int16_t gx1 = x + w - kGaugeInset;
    const int16_t gy0 = y + kGaugeTop;
    const int16_t gy1 = gy0 + kGaugeH;
    const int16_t gw = gx1 - gx0;
    API_COLOR(frame);
    beginLines();
    outline(gx0, gy0, gx1, gy1);
    endLines();

    const int16_t valueX = gx0 + (int16_t)((int32_t)gw * cell.value / 16383);
    const int16_t midX = gx0 + gw / 2;
    if (cell.kind == q4CellUnipolar)
    {
        if (valueX > gx0)
        {
            API_COLOR(ink);
            fillRect(gx0, gy0, valueX, gy1);
        }
    }
    else if (cell.kind == q4CellBipolar)
    {
        API_COLOR(frame);
        beginLines();
        line(midX, gy0, midX, gy1);
        endLines();
        if (valueX != midX)
        {
            API_COLOR(ink);
            fillRect(valueX < midX ? valueX : midX, gy0, valueX < midX ? midX : valueX, gy1);
        }
    }
    else if (cell.kind == q4CellBox)
    {
        API_COLOR(ink);
        uint8_t role = roleToFit(cell.text, gw - 4) == fontMain ? fontSmall : fontTiny;
        if (textWidth(role, cell.text) > gw - 4)
            role = fontTiny;
        const int16_t tw = textWidth(role, cell.text);
        const int16_t tx = tw > gw - 4 ? gx0 + 2 : gx0 + (gw - tw) / 2;
        drawFitted(tx, gy0 + kGaugeH / 2 + 1, role, cell.text, gx1 - 1);
    }

    // modulation of the current slot: a thin bar from the value
    if (cell.flags & q4FlagModulated)
    {
        const int16_t base = cell.kind == q4CellBox ? midX : valueX;
        int16_t end = base + (int16_t)((int32_t)gw * cell.mod / 16383);
        if (end < gx0)
            end = gx0;
        if (end > gx1)
            end = gx1;
        API_COLOR(cfg.modColor);
        fillRect(base < end ? base : end, gy1 + 3, base < end ? end : base, gy1 + 5);
    }
}

//------------------------------------------------------------------------------
// Help
void cQ4Screen::drawHelpLeft()
{
    const Q4Config& cfg = *data->cfg;
    for (uint8_t i = 0; i < q4HelpCount; i++)
    {
        const int16_t x = kFileX[i % 3];
        const int16_t y = kFileY[i / 3];
        const bool selected = i == data->helpPage;
        API_COLOR(selected ? cfg.selectColor : cfg.lineColor);
        drawSheet(x, y);
        API_COLOR(selected ? cfg.selectColor : cfg.textColor);
        const char* title = cfg.helpTitles[i];
        drawCentered(x + kFileW / 2, y + kFileH / 2 + 4, roleToFit(title, kFileW - 6), title);
    }
    API_COLOR(cfg.subTextColor);
    drawCentered(kLeftHintCenter, kHintY + 2, fontSmall, cfg.navHint);
}

void cQ4Screen::drawHelpRight()
{
    const Q4Config& cfg = *data->cfg;
    char heading[32];
    snprintf(heading, sizeof(heading), "> Help.%s", cfg.helpTitles[data->helpPage < q4HelpCount ? data->helpPage : 0]);
    API_COLOR(cfg.textColor);
    drawText(kPaneTitleX, kPaneTitleY, fontMain, heading, kPaneRight);

    if (data->helpPage == q4HelpCredit)
    {
        drawCredit();
        return;
    }

    uint16_t index = 0;
    int16_t y = kHelpFirstY;
    forEachHelpLine(*data, [&](const char* text, uint16_t length, bool) {
        if (index++ < data->helpScroll)
            return true;
        if (y > kScrollBottom)
            return false;
        drawText(kHelpX, y, fontMain, text, kHelpRight + 8, length);
        y += kHelpStep;
        return true;
    });

    drawScrollBar(kScrollX, q4ScreenHelpLineCount(*data), data->helpScroll, kHelpRows, cfg.lineColor);
    API_COLOR(cfg.subTextColor);
    drawCentered(kRightHintCenter, kHintY + 2, fontSmall, cfg.scrollHint);
}

void cQ4Screen::drawCredit()
{
    const Q4Config& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    const int16_t logoW = 36 * kLogoCell;
    drawPixels(kLogo, 20, kCreditCenter - logoW / 2, kLogoY, kLogoCell);
    drawCentered(kCreditCenter, kCreditTaglineY, fontSmall, cfg.tagline);

    int16_t y = kCreditFirstY;
    for (uint8_t i = 0; i < Q4Config::kCreditLines; i++)
    {
        const char* text = cfg.creditLines[i];
        if (text[0] != '\0')
            drawText(kHelpX + (text[0] == '-' ? kCreditIndent : 0), y, fontMain, text, kPaneRight);
        y += kCreditStep;
    }
    API_COLOR(cfg.subTextColor);
    drawCentered(kCreditCenter, kHintY + 2, fontSmall, cfg.copyright);
}

//------------------------------------------------------------------------------
void cQ4Screen::drawQuit()
{
    const Q4Config& cfg = *data->cfg;
    int16_t w = textWidth(fontMain, cfg.quitBefore) + textWidth(fontMain, cfg.quitWord) +
                textWidth(fontMain, cfg.quitAfter);
    int16_t x = (kScreenW - w) / 2;
    API_COLOR(cfg.textColor);
    x = drawText(x, kQuitY, fontMain, cfg.quitBefore, kScreenW - 4);
    API_COLOR(cfg.quitColor);
    x = drawText(x, kQuitY, fontMain, cfg.quitWord, kScreenW - 4);
    API_COLOR(cfg.textColor);
    drawText(x, kQuitY, fontMain, cfg.quitAfter, kScreenW - 4);
}

// The 1 MB flash is nearly full and this screen is not time-critical.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include "soti/sotiScreen.h"

#include <stdio.h>
#include <string.h>

#include "FT812.h"
#include "displayStructs.h"

// Layout of the 800 x 480 screen, measured on the mockup (1928 x 1086,
// x * 0.415, y * 0.442). y values of text are the vertical middle of the line.
// The text and line helpers are the same as the Q-4 screen's.
namespace
{
// Font slots of Soti.ptfont (the Q-4 / Q.T.R.L. layout of make_ptfont.py)
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
constexpr int16_t kTitleMaxX = 220;
constexpr int16_t kNoticeRight = 790;
constexpr int16_t kTitleLineY = 27;

// panes
constexpr int16_t kHeadingY = 66;
constexpr int16_t kHeadingLeftX = 16;
constexpr int16_t kHeadingRightX = 413;
constexpr int16_t kDividerX = 399;
constexpr int16_t kDividerTop = 46;
constexpr int16_t kDividerBottom = 440;
constexpr int16_t kDashOn = 6;
constexpr int16_t kDashOff = 4;
constexpr int16_t kHintY = 424;
constexpr int16_t kLeftCenter = 199;
constexpr int16_t kRightCenter = 591;

// files
constexpr int16_t kFileX[3] = { 25, 137, 249 };
constexpr int16_t kFileY[3] = { 98, 206, 314 };
constexpr int16_t kFileW = 100;
constexpr int16_t kFileH = 82;
constexpr int16_t kFold = 18;
constexpr int16_t kFilesCenter = 199;
constexpr int16_t kScrollX = 373;
constexpr int16_t kScrollW = 13;
constexpr int16_t kScrollTop = 98;
constexpr int16_t kScrollBottom = 396;

// controls: 3 x 5 cells
constexpr int16_t kGridX = 423;
constexpr int16_t kGridY = 98;
constexpr int16_t kCellW = 112;
constexpr int16_t kCellH = 60;
constexpr int16_t kLabelDy = 14;
constexpr int16_t kGaugeInset = 7;
constexpr int16_t kGaugeTop = 27;
constexpr int16_t kGaugeH = 24;

// labels below the screen
constexpr int16_t kLabelsY = 464;
constexpr int16_t kLabelStep = kScreenW / 8;

const SotiScreenData* s_data = nullptr;

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

// ---- shapes -------------------------------------------------------------------
// Sheet with a folded corner; filled when "solid" (the file under the cursor).
void drawSheet(int16_t x, int16_t y, bool solid)
{
    if (solid)
    {
        fillRect(x, y, x + kFileW - kFold, y + kFileH);
        fillRect(x + kFileW - kFold, y + kFold, x + kFileW, y + kFileH);
    }
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

void drawScrollBar(uint16_t totalRows, uint16_t firstRow, uint16_t shownRows, uint32_t color)
{
    API_COLOR(color);
    beginLines();
    outline(kScrollX, kScrollTop, kScrollX + kScrollW, kScrollBottom);
    endLines();
    const int16_t room = kScrollBottom - kScrollTop - 2;
    int16_t y0 = kScrollTop + 1;
    int16_t y1 = kScrollBottom - 1;
    if (totalRows > shownRows)
    {
        y0 = kScrollTop + 1 + (int16_t)((int32_t)room * firstRow / totalRows);
        y1 = kScrollTop + 1 + (int16_t)((int32_t)room * (firstRow + shownRows) / totalRows);
        if (y1 < y0 + 4)
            y1 = y0 + 4;
    }
    if (totalRows > 0)
        fillRect(kScrollX + 1, y0, kScrollX + kScrollW - 1, y1);
}
} // namespace

//------------------------------------------------------------------------------
void sotiScreenReset(SotiScreenData& data)
{
    data.connected = false;
    data.fileCount = 0;
    data.fileTop = 0;
    memset(data.fileFlags, 0, sizeof(data.fileFlags));
    memset(data.fileNames, 0, sizeof(data.fileNames));
    memset(data.values, 0, sizeof(data.values));
    data.select = sotiSelectOne;
    data.statusFlags = 0;
    memset(data.slotNames, 0, sizeof(data.slotNames));
    data.notice[0] = '\0';
    data.quitArmed = false;
}

bool sotiScreenIsSelected(const SotiScreenData& data, uint8_t index)
{
    const uint8_t cols = SotiScreenData::kColumns;
    switch (data.select)
    {
        case sotiSelectRow: return index / cols == data.controlCursor / cols;
        case sotiSelectColumn: return index % cols == data.controlCursor % cols;
        case sotiSelectAll: return true;
        default: return index == data.controlCursor;
    }
}

cSotiScreen::cSotiScreen(strControlProperties* properties)
  : cDisplayControl(controlIdSotiScreen, 1)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    style = properties->style;
    value = properties->value;
    data = (const SotiScreenData*)properties->data;
}

void cSotiScreen::update()
{
    if (data == nullptr || data->cfg == nullptr)
        return;
    s_data = data;

    switch (value)
    {
        case sotiScreenBackground: drawBackground(); break;
        case sotiScreenHeader: drawHeader(); break;
        case sotiScreenLeft: drawFiles(); break;
        case sotiScreenRight: drawControls(); break;
        case sotiScreenLabels: drawLabels(); break;
        default: break;
    }
}

void cSotiScreen::drawBackground()
{
    const SotiConfig& cfg = *data->cfg;

    API_COLOR(cfg.backgroundColor);
    fillRect(0, 0, kScreenW - 1, kScreenH - 1);

    API_COLOR(cfg.lineColor);
    beginLines();
    outline(1, 1, kScreenW - 2, kScreenH - 2);
    line(1, kTitleLineY, kScreenW - 2, kTitleLineY);
    drawDashedVertical(kDividerX, kDividerTop, kDividerBottom, kDashOn, kDashOff);
    endLines();

    API_COLOR(cfg.textColor);
    drawText(kTitleX, kTitleY, fontMain, cfg.title, kTitleMaxX);

    API_COLOR(cfg.subTextColor);
    drawCentered(kLeftCenter, kHintY, fontSmall, cfg.filesHint);
    drawCentered(kRightCenter, kHintY, fontSmall, cfg.controlsHint);
}

void cSotiScreen::drawHeader()
{
    const SotiConfig& cfg = *data->cfg;
    const int16_t left = kTitleMaxX + 12;

    if (!data->connected)
    {
        API_COLOR(cfg.dimColor);
        int16_t w = textWidth(fontSmall, cfg.waiting);
        drawFitted(w < kNoticeRight - left ? kNoticeRight - w : left, kTitleY, fontSmall, cfg.waiting, kNoticeRight);
        return;
    }

    // right end: the value under the cursor
    char value[32];
    const uint8_t cur = data->controlCursor < SotiScreenData::kValues ? data->controlCursor : 0;
    const uint8_t row = cur / SotiScreenData::kColumns;
    const uint8_t col = cur % SotiScreenData::kColumns;
    snprintf(value, sizeof(value), "%s_%s %3d%%", cfg.params[row], cfg.slots[col],
             (int)((data->values[cur] * 100u + 8191u) / 16383u));
    const int16_t valueX = kNoticeRight - textWidth(fontSmall, value);
    API_COLOR(cfg.selectColor);
    drawText(valueX, kTitleY, fontSmall, value, kNoticeRight);

    // before it: a notice, or what is in A B C (and the state of the sound)
    char line[120];
    uint32_t color = cfg.subTextColor;
    if (data->notice[0] != '\0')
    {
        snprintf(line, sizeof(line), "%s", data->notice);
        color = cfg.textColor;
    }
    else
    {
        snprintf(line, sizeof(line), "%s:%s %s:%s %s:%s%s%s", cfg.slots[0], data->slotNames[0], cfg.slots[1],
                 data->slotNames[1], cfg.slots[2], data->slotNames[2],
                 (data->statusFlags & sotiStatusPlaying) ? "" : "  ",
                 (data->statusFlags & sotiStatusPlaying) ? "" : cfg.paused);
    }
    const int16_t room = valueX - 16;
    int16_t w = textWidth(fontSmall, line);
    API_COLOR(color);
    drawFitted(w < room - left ? room - w : left, kTitleY, fontSmall, line, room);
}

void cSotiScreen::drawFile(uint16_t index, int16_t x, int16_t y)
{
    const SotiConfig& cfg = *data->cfg;
    const bool cursor = index == data->fileCursor;
    const bool solid = cursor && data->focus == 0;
    const uint8_t flags = data->fileFlags[index];

    API_COLOR(cursor && !solid ? cfg.selectColor : cfg.lineColor);
    drawSheet(x, y, solid);

    const char* name = data->fileNames[index];
    uint32_t nameColor = solid ? cfg.backgroundColor : cfg.textColor;
    if (!solid && (flags & (sotiFilePending | sotiFileError)))
        nameColor = cfg.dimColor;
    API_COLOR(nameColor);
    const int16_t room = kFileW - 10;
    const uint8_t role = textWidth(fontMain, name) <= room ? fontMain : fontSmall;
    const int16_t w = textWidth(role, name);
    const int16_t nameX = w <= room ? x + (kFileW - w) / 2 : x + 5;
    drawFitted(nameX, y + kFileH / 2 + 3, role, name, x + kFileW - 5);

    // A B C: where this file is loaded
    char marks[8];
    uint8_t n = 0;
    for (uint8_t s = 0; s < 3; s++)
    {
        if (flags & (1 << s))
        {
            marks[n++] = cfg.slots[s][0];
            marks[n++] = ' ';
        }
    }
    marks[n] = '\0';
    if (n > 0)
    {
        API_COLOR(solid ? cfg.backgroundColor : cfg.markColor);
        drawText(x + 6, y + kFileH - 10, fontSmall, marks, x + kFileW - 4);
    }
    if (flags & (sotiFilePending | sotiFileError))
    {
        const char* sign = (flags & sotiFileError) ? "!" : "...";
        API_COLOR((flags & sotiFileError) ? cfg.quitColor : (solid ? cfg.backgroundColor : cfg.subTextColor));
        drawText(x + kFileW - 6 - textWidth(fontSmall, sign), y + kFileH - 10, fontSmall, sign, x + kFileW - 2);
    }
}

void cSotiScreen::drawFiles()
{
    const SotiConfig& cfg = *data->cfg;
    API_COLOR(data->focus == 0 ? cfg.textColor : cfg.dimColor);
    drawFitted(kHeadingLeftX, kHeadingY, fontMain, cfg.filesHeading, kDividerX - 8);

    const uint16_t rows = (data->fileCount + SotiScreenData::kColumns - 1) / SotiScreenData::kColumns;
    drawScrollBar(rows, data->fileTop, SotiScreenData::kFileRows, cfg.lineColor);

    if (data->connected && data->fileCount == 0)
    {
        API_COLOR(cfg.dimColor);
        drawCentered(kFilesCenter, kFileY[1] + kFileH / 2, fontSmall, cfg.noFiles);
        return;
    }
    for (uint8_t r = 0; r < SotiScreenData::kFileRows; r++)
    {
        for (uint8_t c = 0; c < SotiScreenData::kColumns; c++)
        {
            const uint32_t index = (uint32_t)(data->fileTop + r) * SotiScreenData::kColumns + c;
            if (index < data->fileCount)
                drawFile((uint16_t)index, kFileX[c], kFileY[r]);
        }
    }
}

void cSotiScreen::drawControls()
{
    const SotiConfig& cfg = *data->cfg;
    const bool focused = data->focus == 1;
    API_COLOR(focused ? cfg.textColor : cfg.dimColor);
    drawFitted(kHeadingRightX, kHeadingY, fontMain, cfg.controlsHeading, kScreenW - 10);

    const uint8_t cols = SotiScreenData::kColumns;
    const int16_t gridX1 = kGridX + cols * kCellW;
    const int16_t gridY1 = kGridY + SotiConfig::kParams * kCellH;

    // cells the jog changes together (only when more than one)
    if (data->connected && data->select != sotiSelectOne)
    {
        API_COLOR(cfg.selectedFill);
        for (uint8_t i = 0; i < SotiScreenData::kValues; i++)
        {
            if (!sotiScreenIsSelected(*data, i))
                continue;
            const int16_t x = kGridX + (i % cols) * kCellW;
            const int16_t y = kGridY + (i / cols) * kCellH;
            fillRect(x + 1, y + 1, x + kCellW - 1, y + kCellH - 1);
        }
    }

    API_COLOR(cfg.lineColor);
    beginLines();
    outline(kGridX, kGridY, gridX1, gridY1);
    for (uint8_t c = 1; c < cols; c++)
        line(kGridX + c * kCellW, kGridY, kGridX + c * kCellW, gridY1);
    for (uint8_t r = 1; r < SotiConfig::kParams; r++)
        line(kGridX, kGridY + r * kCellH, gridX1, kGridY + r * kCellH);
    endLines();

    char label[20];
    for (uint8_t i = 0; i < SotiScreenData::kValues; i++)
    {
        const int16_t x = kGridX + (i % cols) * kCellW;
        const int16_t y = kGridY + (i / cols) * kCellH;
        const bool cursor = focused && i == data->controlCursor;
        snprintf(label, sizeof(label), "%s_%s", cfg.params[i / cols], cfg.slots[i % cols]);
        API_COLOR(cursor ? cfg.selectColor : cfg.textColor);
        drawFitted(x + kGaugeInset, y + kLabelDy, fontMain, label, x + kCellW - 2);

        const int16_t gx0 = x + kGaugeInset;
        const int16_t gx1 = x + kCellW - kGaugeInset;
        const int16_t gy0 = y + kGaugeTop;
        const int16_t gy1 = y + kGaugeTop + kGaugeH;
        API_COLOR(cursor ? cfg.selectColor : cfg.lineColor);
        beginLines();
        outline(gx0, gy0, gx1, gy1);
        if (cursor)
            outline(gx0 - 1, gy0 - 1, gx1 + 1, gy1 + 1);
        endLines();
        if (!data->connected)
            continue;
        const int16_t fill = (int16_t)((int32_t)(gx1 - gx0 - 3) * data->values[i] / 16383);
        if (fill > 0)
        {
            API_COLOR(cfg.lineColor);
            fillRect(gx0 + 2, gy0 + 2, gx0 + 2 + fill, gy1 - 2);
        }
    }
}

void cSotiScreen::drawLabels()
{
    const SotiConfig& cfg = *data->cfg;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint32_t color = cfg.textColor;
        const char* text = cfg.labels[i];
        if (i == 7)
        {
            color = cfg.quitColor;
            if (data->quitArmed)
                text = cfg.quitAgain;
        }
        else if (i >= 4 && data->select == i - 3)
        {
            color = cfg.selectColor;
        }
        API_COLOR(color);
        const int16_t room = kLabelStep - 4;
        const uint8_t role = textWidth(fontMain, text) <= room ? fontMain : fontSmall;
        const int16_t center = kLabelStep * i + kLabelStep / 2;
        drawText(center - textWidth(role, text) / 2, kLabelsY, role, text, kLabelStep * (i + 1));
    }
}

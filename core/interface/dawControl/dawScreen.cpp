#include "dawControl/dawScreen.h"

#include <string.h>

#include "FT812.h"
#include "displayStructs.h"

// Layout of the 800 x 480 screen. y values are the vertical middle of a line
// of text (the screen font has its capitals in the middle of the cell).
namespace
{
// Font slots of LiveTracker.ptfont (tools/fonts/make_ptfont.py --layout daw)
enum FontRole : uint8_t
{
    fontMain = 0,  // 9 px per character
    fontSmall = 1, // 6 px
    fontTiny = 2,  // 5 px
    fontBold = 3,
    fontSmallBold = 4,
};
// The UI font slots used when the screen font could not be loaded.
constexpr uint8_t kFallbackFont[5] = { 0, 4, 4, 2, 4 };

constexpr uint8_t kCopyrightCell = 127;

constexpr int16_t kScreenW = 800;
constexpr int16_t kScreenH = 480;

constexpr int16_t kTitleX = 19;
constexpr int16_t kTitleY = 13;
constexpr int16_t kTitleLineY = 27;

constexpr int16_t kLogoX = 25;
constexpr int16_t kLogoY = 49;
constexpr int16_t kLogoW = 163; // drawn size of the 131 x 61 unit design
constexpr int16_t kLogoH = 81;
constexpr int16_t kLogoUnitsW = 131;
constexpr int16_t kLogoUnitsH = 61;
// x, y, w, h in logo units
constexpr uint8_t kLogoRects[][4] = {
    { 0, 0, 11, 50 },  { 17, 0, 11, 44 }, { 33, 0, 11, 44 },  { 50, 0, 11, 44 },  { 70, 0, 61, 11 },
    { 95, 11, 11, 50 }, { 110, 17, 21, 11 }, { 70, 17, 21, 11 }, { 70, 33, 21, 11 }, { 110, 33, 21, 11 },
    { 0, 50, 61, 11 }, { 70, 50, 21, 11 }, { 110, 50, 21, 11 },
};

constexpr int16_t kTaglineX = 202;
constexpr int16_t kTaglineY = 91;
constexpr int16_t kCreditRight = 379;
constexpr int16_t kCreditY = 123;

constexpr int16_t kTextX = 22; // description, disclaimer, link
constexpr int16_t kTextRight = 393;
constexpr int16_t kDescriptionY = 148;
constexpr int16_t kDisclaimerY = 163;
constexpr int16_t kSmallLineStep = 14;
constexpr int16_t kMoreDetailY = 202;

constexpr int16_t kInfoTitleX = 28;
constexpr int16_t kInfoTitleY = 238;
constexpr int16_t kInfoLabelX = 38;
constexpr int16_t kInfoValueX = 165;
constexpr int16_t kInfoRight = 392;
constexpr int16_t kInfoRowY[DawControlConfig::kInfoCount] = { 271, 294, 337, 360, 382 };

constexpr int16_t kDividerX = 399;
constexpr int16_t kDividerTop = 42;
constexpr int16_t kDividerBottom = 450;
constexpr int16_t kDashOn = 6;
constexpr int16_t kDashOff = 4;

constexpr int16_t kHelpTitleX = 414;
constexpr int16_t kHelpTitleY = 49;
constexpr int16_t kHelpKeyX = 426;
constexpr int16_t kHelpValueX = 501;
constexpr int16_t kHelpRight = 790;
constexpr int16_t kHelpFirstY = 82;
constexpr int16_t kHelpStepTenths = 223; // 22.3 px per row

constexpr int16_t kLabelsY = 464;
constexpr int16_t kLabelStep = kScreenW / 8;

const DawScreenData* s_data = nullptr;

const strFont& fontFor(uint8_t role)
{
    bool own = (s_data != nullptr && s_data->dawFont);
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

// Word-wraps text between x and maxX, one line every step pixels from y.
void drawWrapped(int16_t x, int16_t y, int16_t step, uint8_t role, const char* text, int16_t maxX)
{
    const int16_t room = maxX - x;
    while (*text)
    {
        while (*text == ' ')
            text++;
        if (*text == '\0')
            break;
        // the longest run of whole words that fits
        const char* lineEnd = text;
        const char* p = text;
        while (*p)
        {
            const char* wordEnd = p;
            while (*wordEnd == ' ')
                wordEnd++;
            while (*wordEnd && *wordEnd != ' ')
                wordEnd++;
            if (textWidth(role, text, (uint16_t)(wordEnd - text)) > room)
                break;
            lineEnd = wordEnd;
            p = wordEnd;
        }
        if (lineEnd == text) // one word longer than the line: cut it
        {
            while (*lineEnd && *lineEnd != ' ')
                lineEnd++;
        }
        drawText(x, y, role, text, maxX, (uint16_t)(lineEnd - text));
        text = lineEnd;
        y += step;
    }
}

void horizontalLine(int16_t x0, int16_t x1, int16_t y)
{
    API_VERTEX2F(x0, y);
    API_VERTEX2F(x1, y);
}
} // namespace

//------------------------------------------------------------------------------
void dawScreenSetDefaultHelp(DawScreenData& data)
{
    // The default buttons of the remote script (ableton/.../mapping.json).
    static constexpr const char* kHelp[][2] = {
        { "Params", "Mixer" },
        { "Perform", "Session / Arrangement" },
        { "File", "Browser" },
        { "Config", "Preferences" },
        { "Master", "Device View of Main Track" },
        { "", "" },
        { "Note", "Clip View" },
        { "Instr", "Device View" },
        { "", "" },
        { "Play", "Play" },
        { "Copy", "Copy" },
        { "Paste", "Paste" },
        { "Insert", "Re-enabling Automation" },
        { "Arrows", "Cursor Movement" },
        { "Delete", "Delete Selection" },
    };
    constexpr uint8_t count = sizeof(kHelp) / sizeof(kHelp[0]);
    for (uint8_t i = 0; i < count; i++)
    {
        strncpy(data.helpKey[i], kHelp[i][0], DawScreenData::kHelpKeyLength - 1);
        data.helpKey[i][DawScreenData::kHelpKeyLength - 1] = '\0';
        strncpy(data.helpValue[i], kHelp[i][1], DawScreenData::kHelpValueLength - 1);
        data.helpValue[i][DawScreenData::kHelpValueLength - 1] = '\0';
    }
    data.helpCount = count;
}

void dawScreenSetHelpLine(DawScreenData& data, uint8_t index, const char* text)
{
    if (index >= DawScreenData::kMaxHelp)
        return;
    if (index == 0)
        data.helpCount = 0;

    // rows the DAW skipped stay blank
    for (uint8_t i = data.helpCount; i < index; i++)
    {
        data.helpKey[i][0] = '\0';
        data.helpValue[i][0] = '\0';
    }

    const char* tab = strchr(text, '\t');
    size_t keyLength = tab ? (size_t)(tab - text) : strlen(text);
    if (keyLength > DawScreenData::kHelpKeyLength - 1)
        keyLength = DawScreenData::kHelpKeyLength - 1;
    memcpy(data.helpKey[index], text, keyLength);
    data.helpKey[index][keyLength] = '\0';

    const char* value = tab ? tab + 1 : "";
    strncpy(data.helpValue[index], value, DawScreenData::kHelpValueLength - 1);
    data.helpValue[index][DawScreenData::kHelpValueLength - 1] = '\0';

    if (index + 1 > data.helpCount)
        data.helpCount = index + 1;
}

//------------------------------------------------------------------------------
cDawScreen::cDawScreen(strControlProperties* properties)
  : cDisplayControl(controlIdDawScreen, 1)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    style = properties->style;
    value = properties->value;
    data = (const DawScreenData*)properties->data;
}

void cDawScreen::update()
{
    if (data == nullptr || data->cfg == nullptr)
        return;
    s_data = data;

    switch (value)
    {
        case dawScreenBackground: drawBackground(); break;
        case dawScreenInfo: drawInfo(); break;
        case dawScreenHelp: drawHelp(); break;
        case dawScreenLabels: drawLabels(); break;
        default: break;
    }
}

void cDawScreen::drawBackground()
{
    const DawControlConfig& cfg = *data->cfg;

    API_COLOR(cfg.backgroundColor);
    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);
    API_VERTEX2F(0, 0);
    API_VERTEX2F(kScreenW - 1, kScreenH - 1);
    API_END();

    // logo
    API_COLOR(cfg.textColor);
    API_BEGIN(RECTS);
    for (const auto& r : kLogoRects)
    {
        int16_t x0 = kLogoX + (r[0] * kLogoW + kLogoUnitsW / 2) / kLogoUnitsW;
        int16_t y0 = kLogoY + (r[1] * kLogoH + kLogoUnitsH / 2) / kLogoUnitsH;
        int16_t x1 = kLogoX + ((r[0] + r[2]) * kLogoW + kLogoUnitsW / 2) / kLogoUnitsW - 1;
        int16_t y1 = kLogoY + ((r[1] + r[3]) * kLogoH + kLogoUnitsH / 2) / kLogoUnitsH - 1;
        API_VERTEX2F(x0, y0);
        API_VERTEX2F(x1, y1);
    }
    API_END();

    // frame, title line and the dashed divider (1 px lines)
    API_COLOR(cfg.lineColor);
    API_LINE_WIDTH(8);
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F(1, 1);
    API_VERTEX2F(kScreenW - 2, 1);
    API_VERTEX2F(kScreenW - 2, kScreenH - 2);
    API_VERTEX2F(1, kScreenH - 2);
    API_VERTEX2F(1, 1);
    API_END();
    API_BEGIN(LINES);
    horizontalLine(1, kScreenW - 2, kTitleLineY);
    for (int16_t y = kDividerTop; y < kDividerBottom; y += kDashOn + kDashOff)
    {
        int16_t end = y + kDashOn;
        if (end > kDividerBottom)
            end = kDividerBottom;
        API_VERTEX2F(kDividerX, y);
        API_VERTEX2F(kDividerX, end);
    }
    API_END();
    API_LINE_WIDTH(16);

    // texts
    API_COLOR(cfg.textColor);
    drawText(kTitleX, kTitleY, fontMain, cfg.title, kScreenW - 10);
    drawText(kTaglineX, kTaglineY, fontMain, cfg.tagline, kDividerX - 4);
    drawText(kInfoTitleX, kInfoTitleY, fontMain, cfg.infoTitle, kDividerX - 4);
    drawText(kHelpTitleX, kHelpTitleY, fontMain, cfg.helpTitle, kScreenW - 10);
    for (uint8_t i = 0; i < DawControlConfig::kInfoCount; i++)
    {
        drawText(kInfoLabelX, kInfoRowY[i], fontMain, cfg.infoLabels[i], kInfoValueX - 2);
    }

    API_COLOR(cfg.subTextColor);
    drawText(kCreditRight - textWidth(fontTiny, cfg.credit), kCreditY, fontTiny, cfg.credit, kDividerX - 4);
    drawWrapped(kTextX, kDescriptionY, kSmallLineStep, fontSmall, cfg.description, kTextRight);
    drawWrapped(kTextX, kDisclaimerY, kSmallLineStep, fontTiny, cfg.disclaimer, kTextRight);
    drawText(kTextX, kMoreDetailY, fontTiny, cfg.moreDetail, kDividerX - 1);
}

void cDawScreen::drawInfo()
{
    API_COLOR(data->cfg->textColor);
    for (uint8_t i = 0; i < DawControlConfig::kInfoCount; i++)
    {
        drawFitted(kInfoValueX, kInfoRowY[i], fontMain, data->info[i], kInfoRight);
    }
}

void cDawScreen::drawHelp()
{
    API_COLOR(data->cfg->textColor);
    for (uint8_t i = 0; i < data->helpCount && i < DawScreenData::kMaxHelp; i++)
    {
        if (data->helpKey[i][0] == '\0' && data->helpValue[i][0] == '\0')
            continue;
        int16_t y = kHelpFirstY + (i * kHelpStepTenths + 5) / 10;
        int16_t x = drawText(kHelpKeyX, y, fontMain, data->helpKey[i], kHelpValueX);
        if (data->helpKey[i][0] != '\0')
            drawText(x, y, fontMain, ":", kHelpValueX);
        drawFitted(kHelpValueX, y, fontMain, data->helpValue[i], kHelpRight);
    }
}

void cDawScreen::drawLabels()
{
    API_COLOR(data->cfg->textColor);
    for (uint8_t i = 0; i < 8; i++)
    {
        const char* text = data->labels[i];
        int16_t center = kLabelStep * i + kLabelStep / 2;
        int16_t x = center - textWidth(fontMain, text) / 2;
        drawText(x, kLabelsY, fontMain, text, kLabelStep * (i + 1));
    }
}

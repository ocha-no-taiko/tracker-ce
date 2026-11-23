#include "qtrl/qtrlScreen.h"

#include <stdio.h>
#include <string.h>

#include "FT812.h"
#include "displayStructs.h"

// Layout of the 800 x 480 screen, measured on the design (640 x 360 units,
// x * 1.25, y * 1.33). y values are the vertical middle of a line of text.
// The text helpers are the same as the DAW Control screen's (dawScreen.cpp).
namespace
{
// Font slots of Qtrl.ptfont (tools/fonts/make_ptfont.py --layout qtrl)
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
constexpr int16_t kPaneTitleY = 72;
constexpr int16_t kPaneX = 448;
constexpr int16_t kPaneFirstY = 117;
constexpr int16_t kPaneRight = 790;
constexpr int16_t kBulletX = 458;
constexpr int16_t kListKeyX = 466;
constexpr int16_t kHintY = 425;

// scroll bars
constexpr int16_t kScrollTop = 53;
constexpr int16_t kScrollBottom = 424;
constexpr int16_t kScrollW = 13;
constexpr int16_t kLeftScrollX = 372;
constexpr int16_t kRightScrollX = 773;
constexpr int16_t kRightTextEnd = 765;

// bottom labels
constexpr int16_t kLabelsY = 464;
constexpr int16_t kLabelStep = kScreenW / 8;

// Home
constexpr int16_t kLogoCell = 13;
constexpr int16_t kLogoX = 129;
constexpr int16_t kLogoY = 205;
constexpr int16_t kLeftCenterX = 207;
constexpr int16_t kTaglineY = 306;
constexpr int16_t kCreditRight = 391;
constexpr int16_t kCreditY = 427;
constexpr int16_t kIntroY = 161;
constexpr int16_t kIntroRight = 720;
constexpr int16_t kLineStep = 23;
constexpr int16_t kHomeListY = 246;
constexpr int16_t kHomeValueX = 530;
constexpr int16_t kListStepTenths = 224;

// Logo (12 x 5) and mascot (5 x 12), one character per cell of the design
constexpr const char* kLogo[5] = {
    "###.#.####..", "#.#.#.#..#..", "#######..#..", "..#.#.#..#..", "..#.#.#..###",
};
constexpr const char* kMascot[12] = {
    "..###", "..#.#", "#####", "..#..", "#####", "..#..",
    "#####", "....#", "....#", "#####", "#....", "#....",
};
constexpr const char* kQuestion[7] = { ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.." };

// Status diagram
constexpr int16_t kBoxW = 101;
constexpr int16_t kBoxH = 54;
constexpr int16_t kLeftBoxX = 44;
constexpr int16_t kRightBoxX = 250;
constexpr int16_t kBoxRowY[3] = { 105, 211, 317 };
constexpr int16_t kJunctionX = 197;
// node slot -> box (x, y)
constexpr int16_t kNodeBox[QtrlConfig::kNodeCount][2] = {
    { kLeftBoxX, kBoxRowY[0] },  // Terminal
    { kLeftBoxX, kBoxRowY[1] },  // QTRL1
    { kLeftBoxX, kBoxRowY[2] },  // QTRL2
    { kRightBoxX, kBoxRowY[0] }, // FAB1
    { kRightBoxX, kBoxRowY[1] }, // Server
};
constexpr int16_t kSystemValueX = 572;
constexpr int16_t kCheckDetailY = 140;
constexpr int16_t kStatusListY = 194;
constexpr int16_t kStatusStep = 22;
constexpr int16_t kStatusValueX = 551;
constexpr int16_t kDetailY = 318;
constexpr int16_t kDetailStep = 16;

// Logs
constexpr int16_t kFileX[3] = { 20, 131, 243 };
constexpr int16_t kFileY[3] = { 53, 172, 291 };
constexpr int16_t kFileW = 100;
constexpr int16_t kFileH = 107;
constexpr int16_t kFold = 25;
constexpr int16_t kLeftHintX = 22;
constexpr int16_t kLogTimeX = 452;
constexpr int16_t kLogTextX = 526;
constexpr int16_t kLogFirstY = 159;
constexpr int16_t kLogStep = 22;

// Chat
constexpr int16_t kSmallLogoCell = 4;
constexpr int16_t kSmallLogoX = 299;
constexpr int16_t kSmallLogoY = 227;
constexpr int16_t kInputX0 = 50;
constexpr int16_t kInputX1 = 350;
constexpr int16_t kInputY0 = 251;
constexpr int16_t kInputY1 = 278;
constexpr int16_t kKeysX = 50;
constexpr int16_t kKeysY = 291;
constexpr int16_t kKeyW = 25;
constexpr int16_t kKeyH = 26;
constexpr int16_t kChatHintY = 418;
constexpr int16_t kChatStep = 20;

// Help
constexpr int16_t kMascotX = 168;
constexpr int16_t kMascotY = 165;
constexpr int16_t kHelpListY = 156;
constexpr int16_t kHelpValueX = 568;

// QUIT
constexpr int16_t kQuitY = 237;

const QtrlScreenData* s_data = nullptr;

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
    if (lineEnd == text && *text && *text != '\n') // one word longer than the line: cut it
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

// Word-wraps text between x and maxX, one line every step pixels from y.
void drawWrapped(int16_t x, int16_t y, int16_t step, uint8_t role, const char* text, int16_t maxX)
{
    while (*text)
    {
        while (*text == ' ')
            text++;
        if (*text == '\0')
            break;
        uint16_t n = fitWords(role, text, maxX - x);
        drawText(x, y, role, text, maxX, n);
        text += n;
        y += step;
    }
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
            // merge a run of filled cells into one rectangle
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

void drawBullet(int16_t y)
{
    fillRect(kBulletX - 1, y - 1, kBulletX, y);
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

// Scroll bar: outline, and the visible part [first, first + shown) of total filled.
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

uint32_t stateColor(const QtrlConfig& cfg, uint8_t state)
{
    if (state == qtrlStateOk)
        return cfg.okColor;
    if (state == qtrlStateAnomaly)
        return cfg.alertColor;
    return cfg.unknownColor;
}

int16_t listY(int16_t first, uint8_t i)
{
    return first + (i * kListStepTenths + 5) / 10;
}

void drawList(const char (*keys)[QtrlConfig::kListKeyLength], const char (*values)[QtrlConfig::kListValueLength],
              int16_t firstY, int16_t valueX)
{
    for (uint8_t i = 0; i < QtrlConfig::kListRows; i++)
    {
        if (keys[i][0] == '\0' && values[i][0] == '\0')
            continue;
        int16_t y = listY(firstY, i);
        if (keys[i][0] != '\0')
        {
            drawBullet(y);
            drawText(kListKeyX, y, fontMain, keys[i], valueX - 2);
        }
        drawFitted(valueX, y, fontMain, values[i], kPaneRight);
    }
}

bool isSpecialKey(const char* key)
{
    return strcmp(key, "BS") == 0 || strcmp(key, "CAPS") == 0 || strcmp(key, "SP") == 0;
}

const char* specialLabel(const char* key)
{
    if (strcmp(key, "BS") == 0)
        return "Backspace";
    if (strcmp(key, "CAPS") == 0)
        return "Caps Lock";
    return "Space";
}

bool isMarked(const QtrlConfig& cfg, uint8_t pad)
{
    for (uint8_t i = 0; i < cfg.markedCount; i++)
    {
        if (cfg.markedKeys[i] == pad)
            return true;
    }
    return false;
}
} // namespace

//------------------------------------------------------------------------------
void qtrlScreenReset(QtrlScreenData& data)
{
    const QtrlConfig& cfg = *data.cfg;
    data.connected = false;
    data.notice[0] = '\0';
    for (uint8_t i = 0; i < QtrlConfig::kNodeCount; i++)
    {
        QtrlScreenData::Node& n = data.nodes[i];
        n.state = qtrlStateUnknown;
        snprintf(n.name, sizeof(n.name), "%s", cfg.nodeNames[i]);
        snprintf(n.sub, sizeof(n.sub), "%s", cfg.nodeSubs[i]);
        snprintf(n.status, sizeof(n.status), "%s", cfg.statusUnknown);
    }
    data.systemState = qtrlStateUnknown;
    snprintf(data.systemStatus, sizeof(data.systemStatus), "%s", cfg.waiting);
    data.detailCount = 0;
    data.dateCount = 0;
    data.selectedDate = -1;
    data.dateTopRow = 0;
    data.logDate[0] = '\0';
    data.logLoading = false;
    data.logCount = 0;
    data.logTop = 0;
    qtrlScreenChatClear(data);
    data.chatBusy = false;
}

void qtrlScreenChatClear(QtrlScreenData& data)
{
    data.chatStart = 0;
    data.chatCount = 0;
    data.chatScroll = 0;
}

const char* qtrlScreenChatLine(const QtrlScreenData& data, uint16_t index)
{
    return data.chat[(data.chatStart + index) % QtrlScreenData::kMaxChatLines];
}

static void chatPush(QtrlScreenData& data, const char* text, uint16_t length)
{
    if (length >= QtrlScreenData::kChatLineLength)
        length = QtrlScreenData::kChatLineLength - 1;
    uint16_t slot;
    if (data.chatCount < QtrlScreenData::kMaxChatLines)
    {
        slot = (data.chatStart + data.chatCount) % QtrlScreenData::kMaxChatLines;
        data.chatCount++;
    }
    else
    {
        slot = data.chatStart;
        data.chatStart = (data.chatStart + 1) % QtrlScreenData::kMaxChatLines;
    }
    memcpy(data.chat[slot], text, length);
    data.chat[slot][length] = '\0';
}

void qtrlScreenChatAdd(QtrlScreenData& data, const char* speaker, const char* text)
{
    s_data = &data;
    char head[QtrlScreenData::kChatLineLength];
    snprintf(head, sizeof(head), "%s:", speaker);
    chatPush(data, head, (uint16_t)strlen(head));

    const int16_t room = kRightTextEnd - kPaneX;
    while (*text)
    {
        if (*text == '\n')
        {
            text++;
            if (*text == '\n' || *text == '\0')
                chatPush(data, "", 0); // blank line in the text
            continue;
        }
        uint16_t n = fitWords(fontMain, text, room);
        chatPush(data, text, n);
        text += n;
        while (*text == ' ')
            text++;
    }
    chatPush(data, "", 0);
    data.chatScroll = 0;
}

const char* qtrlScreenKey(const QtrlScreenData& data, uint8_t pad)
{
    const QtrlConfig& cfg = *data.cfg;
    uint8_t row = pad / QtrlConfig::kKeyColumns;
    uint8_t col = pad % QtrlConfig::kKeyColumns;
    if (row >= QtrlConfig::kKeyRows)
        return "";
    return data.shift ? cfg.shiftKeys[row][col] : cfg.keys[row][col];
}

//------------------------------------------------------------------------------
cQtrlScreen::cQtrlScreen(strControlProperties* properties)
  : cDisplayControl(controlIdQtrlScreen, 1)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    style = properties->style;
    value = properties->value;
    data = (const QtrlScreenData*)properties->data;
}

void cQtrlScreen::update()
{
    if (data == nullptr || data->cfg == nullptr)
        return;
    s_data = data;

    switch (value)
    {
        case qtrlScreenBackground: drawBackground(); break;
        case qtrlScreenHeader: drawHeader(); break;
        case qtrlScreenLeft: drawLeft(); break;
        case qtrlScreenRight: drawRight(); break;
        case qtrlScreenLabels: drawLabels(); break;
        default: break;
    }
}

void cQtrlScreen::drawBackground()
{
    const QtrlConfig& cfg = *data->cfg;

    API_COLOR(cfg.backgroundColor);
    fillRect(0, 0, kScreenW - 1, kScreenH - 1);

    // frame, title line and the dashed divider (1 px lines)
    API_COLOR(cfg.lineColor);
    beginLines();
    outline(1, 1, kScreenW - 2, kScreenH - 2);
    line(1, kTitleLineY, kScreenW - 2, kTitleLineY);
    if (data->tab != qtrlTabQuit)
        drawDashedVertical(kDividerX, kDividerTop, kDividerBottom, kDashOn, kDashOff);
    endLines();

    API_COLOR(cfg.textColor);
    drawText(kTitleX, kTitleY, fontMain, cfg.title, kHeadingX - 4);
}

void cQtrlScreen::drawHeader()
{
    const QtrlConfig& cfg = *data->cfg;
    char heading[24];
    snprintf(heading, sizeof(heading), "> %s", cfg.headings[data->tab < qtrlTabCount ? data->tab : 0]);
    API_COLOR(cfg.textColor);
    int16_t x = drawText(kHeadingX, kTitleY, fontMain, heading, kScreenW - 10);

    const char* notice = data->connected ? data->notice : cfg.waiting;
    if (notice[0] != '\0')
    {
        API_COLOR(data->connected ? cfg.subTextColor : cfg.unknownColor);
        int16_t w = textWidth(fontSmall, notice);
        int16_t nx = kNoticeRight - w;
        if (nx < x + 16)
            nx = x + 16;
        drawFitted(nx, kTitleY, fontSmall, notice, kNoticeRight);
    }
}

void cQtrlScreen::drawLeft()
{
    switch (data->tab)
    {
        case qtrlTabHome: drawHomeLeft(); break;
        case qtrlTabStatus: drawStatusLeft(); break;
        case qtrlTabLogs: drawLogsLeft(); break;
        case qtrlTabChat: drawChatLeft(); break;
        case qtrlTabHelp: drawHelpLeft(); break;
        case qtrlTabQuit: drawQuit(); break;
        default: break;
    }
}

void cQtrlScreen::drawRight()
{
    const QtrlConfig& cfg = *data->cfg;
    if (data->tab == qtrlTabQuit)
        return;

    char heading[24];
    snprintf(heading, sizeof(heading), "> %s", cfg.headings[data->tab < qtrlTabCount ? data->tab : 0]);
    API_COLOR(cfg.textColor);
    drawText(kPaneTitleX, kPaneTitleY, fontMain, heading, kPaneRight);

    switch (data->tab)
    {
        case qtrlTabHome: drawHomeRight(); break;
        case qtrlTabStatus: drawStatusRight(); break;
        case qtrlTabLogs: drawLogsRight(); break;
        case qtrlTabChat: drawChatRight(); break;
        case qtrlTabHelp: drawHelpRight(); break;
        default: break;
    }
}

void cQtrlScreen::drawLabels()
{
    const QtrlConfig& cfg = *data->cfg;

    if (data->tab == qtrlTabQuit)
    {
        // only "Cancel" (button 7) and "QUIT" (button 8)
        int16_t center = kLabelStep * 6 + kLabelStep / 2;
        API_COLOR(cfg.textColor);
        drawCentered(center, kLabelsY - 7, fontMain, cfg.cancel);
        API_COLOR(cfg.subTextColor);
        drawCentered(center, kLabelsY + 8, fontTiny, cfg.cancelSub);
        API_COLOR(cfg.quitColor);
        drawCentered(kLabelStep * 7 + kLabelStep / 2, kLabelsY, fontMain, data->labels[7]);
        return;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t action = cfg.screenButtons[i];
        uint32_t color = cfg.textColor;
        if (action == qtrlActionQuit)
            color = cfg.quitColor;
        else if (action >= qtrlActionHome && action <= qtrlActionHelp &&
                 action - qtrlActionHome == data->tab)
            color = cfg.selectColor;
        API_COLOR(color);
        const char* text = data->labels[i];
        int16_t center = kLabelStep * i + kLabelStep / 2;
        drawText(center - textWidth(fontMain, text) / 2, kLabelsY, fontMain, text, kLabelStep * (i + 1));
    }
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawHomeLeft()
{
    const QtrlConfig& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    drawPixels(kLogo, 5, kLogoX, kLogoY, kLogoCell);
    drawCentered(kLeftCenterX, kTaglineY, fontSmall, cfg.tagline);
    API_COLOR(cfg.subTextColor);
    drawText(kCreditRight - textWidth(fontTiny, cfg.credit), kCreditY, fontTiny, cfg.credit, kDividerX - 2);
}

void cQtrlScreen::drawHomeRight()
{
    const QtrlConfig& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    drawText(kPaneX, kPaneFirstY, fontMain, cfg.welcome, kPaneRight);
    drawWrapped(kPaneX, kIntroY, kLineStep, fontMain, cfg.intro, kIntroRight);
    drawList(cfg.homeKeys, cfg.homeValues, kHomeListY, kHomeValueX);
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawStatusLeft()
{
    const QtrlConfig& cfg = *data->cfg;

    // connections: the three on the left join and run into the server
    const int16_t leftEdge = kLeftBoxX + kBoxW;
    const int16_t mid[3] = { kBoxRowY[0] + kBoxH / 2, kBoxRowY[1] + kBoxH / 2, kBoxRowY[2] + kBoxH / 2 };
    API_COLOR(cfg.lineColor);
    beginLines();
    line(leftEdge, mid[0], kJunctionX, mid[0]);
    line(kJunctionX, mid[0], kJunctionX, mid[2]);
    line(leftEdge, mid[2], kJunctionX, mid[2]);
    line(leftEdge, mid[1], kRightBoxX - 1, mid[1]);
    line(kRightBoxX - 1, mid[1], kRightBoxX - 8, mid[1] - 4); // arrow head
    line(kRightBoxX - 1, mid[1], kRightBoxX - 8, mid[1] + 4);
    // FAB1 - Server over Wi-Fi (double dotted line)
    const int16_t wifiX = kRightBoxX + kBoxW / 2;
    drawDashedVertical(wifiX - 2, kBoxRowY[0] + kBoxH + 2, kBoxRowY[1] - 1, 2, 2);
    drawDashedVertical(wifiX + 2, kBoxRowY[0] + kBoxH + 2, kBoxRowY[1] - 1, 2, 2);
    endLines();
    API_COLOR(cfg.textColor);
    drawText(wifiX + 10, (kBoxRowY[0] + kBoxH + kBoxRowY[1]) / 2, fontSmall, "WiFi", kDividerX - 4);

    for (uint8_t i = 0; i < QtrlConfig::kNodeCount; i++)
    {
        const QtrlScreenData::Node& n = data->nodes[i];
        const int16_t x = kNodeBox[i][0];
        const int16_t y = kNodeBox[i][1];
        API_COLOR(stateColor(cfg, n.state));
        beginLines();
        outline(x, y, x + kBoxW, y + kBoxH);
        endLines();
        API_COLOR(cfg.textColor);
        drawCentered(x + kBoxW / 2, y + 17, fontMain, n.name);
        drawCentered(x + kBoxW / 2, y + 38, fontTiny, n.sub);
    }
}

void cQtrlScreen::drawStatusRight()
{
    const QtrlConfig& cfg = *data->cfg;

    API_COLOR(cfg.textColor);
    drawText(kPaneX, kPaneFirstY, fontMain, cfg.systemLabel, kSystemValueX - 2);
    API_COLOR(stateColor(cfg, data->systemState));
    drawFitted(kSystemValueX, kPaneFirstY, fontMain, data->systemStatus, kPaneRight);
    if (data->systemState == qtrlStateAnomaly)
    {
        API_COLOR(cfg.subTextColor);
        drawText(kPaneX, kCheckDetailY, fontSmall, cfg.checkDetail, kPaneRight);
    }

    for (uint8_t i = 0; i < QtrlConfig::kNodeCount; i++)
    {
        const QtrlScreenData::Node& n = data->nodes[i];
        int16_t y = kStatusListY + i * kStatusStep;
        char key[16];
        snprintf(key, sizeof(key), "%s:", n.name);
        API_COLOR(cfg.textColor);
        drawBullet(y);
        drawText(kListKeyX, y, fontMain, key, kStatusValueX - 2);
        API_COLOR(stateColor(cfg, n.state));
        drawFitted(kStatusValueX, y, fontMain, n.status, kPaneRight);
    }

    API_COLOR(cfg.subTextColor);
    for (uint8_t i = 0; i < data->detailCount && i < QtrlScreenData::kMaxDetail; i++)
    {
        drawFitted(kPaneX, kDetailY + i * kDetailStep, fontSmall, data->detail[i], kPaneRight);
    }
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawLogsLeft()
{
    const QtrlConfig& cfg = *data->cfg;
    const uint8_t first = data->dateTopRow * QtrlScreenData::kDateColumns;

    for (uint8_t slot = 0; slot < QtrlScreenData::kDateColumns * QtrlScreenData::kDateRows; slot++)
    {
        uint16_t index = first + slot;
        if (index >= data->dateCount)
            break;
        const int16_t x = kFileX[slot % QtrlScreenData::kDateColumns];
        const int16_t y = kFileY[slot / QtrlScreenData::kDateColumns];
        const bool selected = (int16_t)index == data->selectedDate;
        API_COLOR(selected ? cfg.selectColor : cfg.lineColor);
        beginLines();
        // sheet with a folded corner
        line(x, y, x + kFileW - kFold, y);
        line(x + kFileW - kFold, y, x + kFileW, y + kFold);
        line(x + kFileW, y + kFold, x + kFileW, y + kFileH);
        line(x + kFileW, y + kFileH, x, y + kFileH);
        line(x, y + kFileH, x, y);
        line(x + kFileW - kFold, y, x + kFileW - kFold, y + kFold);
        line(x + kFileW - kFold, y + kFold, x + kFileW, y + kFold);
        endLines();
        API_COLOR(selected ? cfg.selectColor : cfg.textColor);
        drawCentered(x + kFileW / 2, y + kFileH / 2, fontMain, data->dates[index]);
    }

    uint16_t rows = (data->dateCount + QtrlScreenData::kDateColumns - 1) / QtrlScreenData::kDateColumns;
    drawScrollBar(kLeftScrollX, rows, data->dateTopRow, QtrlScreenData::kDateRows, cfg.lineColor);

    API_COLOR(cfg.subTextColor);
    drawText(kLeftHintX, kHintY, fontSmall, cfg.logsHint, kDividerX - 4);
}

void cQtrlScreen::drawLogsRight()
{
    const QtrlConfig& cfg = *data->cfg;

    API_COLOR(cfg.textColor);
    drawText(kPaneX, kPaneFirstY, fontMain, data->logDate, kPaneRight);

    if (data->logLoading || data->logCount == 0)
    {
        API_COLOR(cfg.subTextColor);
        const char* text = data->logLoading ? cfg.logLoading : (data->dateCount ? cfg.logEmpty : "");
        drawText(kLogTimeX, kLogFirstY, fontMain, text, kRightTextEnd);
    }
    else
    {
        API_COLOR(cfg.textColor);
        for (uint8_t i = 0; i < QtrlScreenData::kLogRows; i++)
        {
            uint16_t index = data->logTop + i;
            if (index >= data->logCount)
                break;
            int16_t y = kLogFirstY + i * kLogStep;
            drawText(kLogTimeX, y, fontMain, data->logTime[index], kLogTextX - 2);
            drawFitted(kLogTextX, y, fontMain, data->logText[index], kRightTextEnd);
        }
    }

    drawScrollBar(kRightScrollX, data->logCount, data->logTop, QtrlScreenData::kLogRows, cfg.lineColor);
    API_COLOR(cfg.subTextColor);
    drawText(kPaneTitleX, kHintY, fontSmall, cfg.logHint, kRightScrollX - 4);
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawChatLeft()
{
    const QtrlConfig& cfg = *data->cfg;
    const int16_t keysRight = kKeysX + kKeyW * QtrlConfig::kKeyColumns;

    API_COLOR(cfg.textColor);
    drawPixels(kLogo, 5, kSmallLogoX, kSmallLogoY, kSmallLogoCell);

    // filled keys: home position marks, the key being pressed, Caps Lock when on
    for (uint8_t pad = 0; pad < QtrlConfig::kKeyRows * QtrlConfig::kKeyColumns; pad++)
    {
        const char* key = cfg.keys[pad / QtrlConfig::kKeyColumns][pad % QtrlConfig::kKeyColumns];
        bool filled = isMarked(cfg, pad) || pad == data->pressedKey || (data->caps && strcmp(key, "CAPS") == 0);
        if (!filled)
            continue;
        int16_t x = kKeysX + (pad % QtrlConfig::kKeyColumns) * kKeyW;
        int16_t y = kKeysY + (pad / QtrlConfig::kKeyColumns) * kKeyH;
        API_COLOR(pad == data->pressedKey ? cfg.selectColor : cfg.textColor);
        fillRect(x, y, x + kKeyW, y + kKeyH);
    }

    // input box, return mark and the key grid
    API_COLOR(cfg.lineColor);
    beginLines();
    outline(kInputX0, kInputY0, kInputX1, kInputY1);
    line(382, 253, 382, 264);
    line(382, 264, 362, 264);
    line(362, 264, 368, 259);
    line(362, 264, 368, 269);
    for (uint8_t r = 0; r <= QtrlConfig::kKeyRows; r++)
        line(kKeysX, kKeysY + r * kKeyH, keysRight, kKeysY + r * kKeyH);
    for (uint8_t r = 0; r < QtrlConfig::kKeyRows; r++)
    {
        const int16_t y0 = kKeysY + r * kKeyH;
        for (uint8_t c = 0; c <= QtrlConfig::kKeyColumns; c++)
        {
            // no line inside a wide key (two pads with the same special key)
            if (c > 0 && c < QtrlConfig::kKeyColumns && isSpecialKey(cfg.keys[r][c]) &&
                strcmp(cfg.keys[r][c], cfg.keys[r][c - 1]) == 0)
                continue;
            line(kKeysX + c * kKeyW, y0, kKeysX + c * kKeyW, y0 + kKeyH);
        }
    }
    endLines();

    // key labels
    for (uint8_t r = 0; r < QtrlConfig::kKeyRows; r++)
    {
        for (uint8_t c = 0; c < QtrlConfig::kKeyColumns; c++)
        {
            const uint8_t pad = r * QtrlConfig::kKeyColumns + c;
            const char* key = qtrlScreenKey(*data, pad);
            const char* base = cfg.keys[r][c];
            const int16_t y = kKeysY + r * kKeyH + kKeyH / 2;
            bool filled = isMarked(cfg, pad) || pad == data->pressedKey ||
                          (data->caps && strcmp(base, "CAPS") == 0);
            API_COLOR(filled ? cfg.backgroundColor : cfg.textColor);
            if (isSpecialKey(base))
            {
                if (c > 0 && strcmp(cfg.keys[r][c - 1], base) == 0)
                    continue; // drawn with the first pad of the key
                uint8_t span = 1;
                while (c + span < QtrlConfig::kKeyColumns && strcmp(cfg.keys[r][c + span], base) == 0)
                    span++;
                drawCentered(kKeysX + c * kKeyW + span * kKeyW / 2, y, fontTiny, specialLabel(base));
                continue;
            }
            char label[QtrlConfig::kKeyLength];
            snprintf(label, sizeof(label), "%s", key);
            if (data->caps && !data->shift && label[0] >= 'a' && label[0] <= 'z' && label[1] == '\0')
                label[0] = (char)(label[0] - 'a' + 'A');
            drawCentered(kKeysX + c * kKeyW + kKeyW / 2, y, fontMain, label);
        }
    }

    // input text: the end that fits, with a cursor; new lines show as '|'
    char shown[QtrlScreenData::kInputLength + 2];
    uint16_t n = 0;
    for (uint16_t i = 0; i < data->inputLength && n < QtrlScreenData::kInputLength; i++)
        shown[n++] = data->input[i] == '\n' ? '|' : data->input[i];
    shown[n++] = '_';
    shown[n] = '\0';
    const char* start = shown;
    const int16_t room = kInputX1 - kInputX0 - 8;
    while (*start && textWidth(fontMain, start) > room)
        start++;
    API_COLOR(cfg.textColor);
    drawText(kInputX0 + 4, (kInputY0 + kInputY1) / 2, fontMain, start, kInputX1 - 2);

    API_COLOR(cfg.subTextColor);
    drawWrapped(kInputX0, kChatHintY, 12, fontTiny, cfg.chatHint, kDividerX - 6);
}

void cQtrlScreen::drawChatRight()
{
    const QtrlConfig& cfg = *data->cfg;
    const uint16_t total = data->chatCount + (data->chatBusy ? 1 : 0);
    uint16_t first = 0;
    if (total > QtrlScreenData::kChatRows)
    {
        uint16_t maxFirst = total - QtrlScreenData::kChatRows;
        first = data->chatScroll > maxFirst ? 0 : maxFirst - data->chatScroll;
    }

    API_COLOR(cfg.textColor);
    for (uint8_t i = 0; i < QtrlScreenData::kChatRows; i++)
    {
        uint16_t index = first + i;
        if (index >= total)
            break;
        int16_t y = kPaneFirstY + i * kChatStep;
        if (index >= data->chatCount)
        {
            API_COLOR(cfg.subTextColor);
            drawText(kPaneX, y, fontMain, cfg.chatBusy, kRightTextEnd);
            break;
        }
        drawText(kPaneX, y, fontMain, qtrlScreenChatLine(*data, index), kRightTextEnd);
    }

    drawScrollBar(kRightScrollX, total, first, QtrlScreenData::kChatRows, cfg.lineColor);
    API_COLOR(cfg.subTextColor);
    drawText(kPaneTitleX, kHintY, fontSmall, cfg.logHint, kRightScrollX - 4);
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawHelpLeft()
{
    const QtrlConfig& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    drawPixels(kMascot, 12, kMascotX, kMascotY, kLogoCell);
    drawPixels(kQuestion, 7, 256, 103, 4);

    // speech bubble with its tail towards the mascot
    API_COLOR(cfg.lineColor);
    beginLines();
    line(282, 146, 297, 146);
    line(297, 146, 297, 86);
    line(297, 86, 234, 86);
    line(234, 86, 234, 146);
    line(234, 146, 253, 146);
    line(253, 146, 253, 167);
    line(253, 167, 282, 146);
    endLines();

    API_COLOR(cfg.subTextColor);
    drawText(kCreditRight - textWidth(fontTiny, cfg.credit), kCreditY, fontTiny, cfg.credit, kDividerX - 2);
}

void cQtrlScreen::drawHelpRight()
{
    const QtrlConfig& cfg = *data->cfg;
    API_COLOR(cfg.textColor);
    drawList(cfg.helpKeys, cfg.helpValues, kHelpListY, kHelpValueX);
}

//------------------------------------------------------------------------------
void cQtrlScreen::drawQuit()
{
    const QtrlConfig& cfg = *data->cfg;
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

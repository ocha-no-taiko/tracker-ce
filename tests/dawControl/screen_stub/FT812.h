#pragma once
// Host stub: records the display list so preview_screen.py can draw it.
#include <cstdint>
#include <cstdio>
enum { BITMAPS = 1, POINTS = 2, LINES = 3, LINE_STRIP = 4, RECTS = 9 };
extern FILE* g_list;
extern unsigned g_words;
inline void listWord(const char* fmt, long a = 0, long b = 0, long c = 0, long d = 0)
{
    g_words++;
    fprintf(g_list, fmt, a, b, c, d);
    fputc('\n', g_list);
}
inline void API_COLOR(uint32_t rgb) { listWord("color %ld", (long)rgb); }
inline void API_LINE_WIDTH(uint16_t w) { listWord("linewidth %ld", w); }
inline void API_BEGIN(uint8_t p) { listWord("begin %ld", p); }
inline void API_END() { listWord("end"); }
inline void API_VERTEX2F(int16_t x, int16_t y) { listWord("v2f %ld %ld", x, y); }
inline void API_VERTEX2II(uint16_t x, uint16_t y, uint8_t h, uint8_t c)
{
    if (x > 511 || y > 511) { fprintf(stderr, "VERTEX2II out of range %u %u\n", x, y); }
    listWord("v2ii %ld %ld %ld %ld", x, y, h, c);
}
inline void API_BITMAP_HANDLE(uint8_t h) { listWord("handle %ld", h); }
inline void API_CELL(uint8_t c) { listWord("cell %ld", c); }

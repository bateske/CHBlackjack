#pragma GCC optimize("Os")
#include "Draw.h"
#include "../RamFunc.h"   // hot loops run from SRAM

static inline void plot(uint8_t *p, int x, uint8_t c) {
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else       *p = (uint8_t)((*p & 0xF0) | c);
}

// Corner insets per row for radius 1..4 (pixel-art circles, not chamfers).
// CHGfx 1.3's gfx_fillRoundRect/gfx_roundRect draw the same pixels for all
// but one of the game's rects, but work the insets out with isqrt on every
// call. These two take 390 B against the library's 524 B in the release
// build, and are faster. (The exception: the library caps r to half the
// width, so a 4 px mid-flip card needed a special case.)
static const uint8_t INSET[4][4] = { {1}, {2, 1}, {3, 1, 1}, {4, 2, 1, 1} };

void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c) {
    if (r > 4) r = 4;
    if (r * 2 > h) r = (uint8_t)(h / 2);
    const uint8_t *in = INSET[r ? r - 1 : 0];
    for (int i = 0; i < r; i++) {
        gfx_hline(x + in[i], y + i, w - 2 * in[i], c);
        gfx_hline(x + in[i], y + h - 1 - i, w - 2 * in[i], c);
    }
    gfx_fillRect(x, y + r, w, h - 2 * r, c);
}

void roundRect(int x, int y, int w, int h, uint8_t r, uint8_t c) {
    if (r > 4) r = 4;
    if (r * 2 > h) r = (uint8_t)(h / 2);
    if (!r) { gfx_rect(x, y, w, h, c); return; }
    const uint8_t *in = INSET[r - 1];
    gfx_hline(x + in[0], y, w - 2 * in[0], c);
    gfx_hline(x + in[0], y + h - 1, w - 2 * in[0], c);
    for (uint8_t i = 1; i < r; i++) {
        int len = in[i - 1] - in[i]; if (len < 1) len = 1;
        gfx_hline(x + in[i], y + i, len, c);
        gfx_hline(x + w - in[i] - len, y + i, len, c);
        gfx_hline(x + in[i], y + h - 1 - i, len, c);
        gfx_hline(x + w - in[i] - len, y + h - 1 - i, len, c);
    }
    gfx_vline(x, y + r, h - 2 * r, c);
    gfx_vline(x + w - 1, y + r, h - 2 * r, c);
}

void panel(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge) {
    fillRound(x, y, w, h, r, fill);
    roundRect(x, y, w, h, r, edge);
}

// CHGfx 1.3's gfx_remapRect does the same, but from SRAM: in the release
// build it cost 304 B more SRAM and 182 B more flash than this, for the one
// rectangle that dims the waiting split hand (not a hot path).
void remapRect(int x, int y, int w, int h, const uint8_t *m) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint8_t *row = gfx_fb + (y + j) * GFX_FB_STRIDE;
        int i = x;
        if (i & 1) { uint8_t b = row[i >> 1]; row[i >> 1] = (uint8_t)((b & 0x0F) | (m[b >> 4] << 4)); i++; }
        for (; i + 1 < x + w; i += 2) { uint8_t b = row[i >> 1]; row[i >> 1] = (uint8_t)(m[b & 15] | (m[b >> 4] << 4)); }
        if (i < x + w) { uint8_t b = row[i >> 1]; row[i >> 1] = (uint8_t)((b & 0xF0) | m[b & 15]); }
    }
}

// ---------------------------------------------------------------------------
// PPOT Font3x5 (Press Play On Tape, Apache-2.0). Column bytes, bit 0 = top,
// bit 5 = descender. Extended here with $ , / ' * ( ) < > = % #.
// ---------------------------------------------------------------------------
const uint8_t FONT35[][3] = {
    {0x1F,0x05,0x1F},{0x1F,0x15,0x1B},{0x1F,0x11,0x11},{0x1F,0x11,0x0E},{0x1F,0x15,0x11},  // A-E
    {0x1F,0x05,0x01},{0x1F,0x11,0x1D},{0x1F,0x04,0x1F},{0x00,0x1F,0x00},{0x10,0x10,0x1F},  // F-J
    {0x1F,0x04,0x1B},{0x1F,0x10,0x10},{0x1F,0x06,0x1F},{0x1F,0x01,0x1F},{0x1F,0x11,0x1F},  // K-O
    {0x1F,0x05,0x07},{0x1F,0x31,0x1F},{0x1F,0x05,0x1B},{0x17,0x15,0x1D},{0x01,0x1F,0x01},  // P-T
    {0x1F,0x10,0x1F},{0x0F,0x10,0x0F},{0x1F,0x0C,0x1F},{0x1B,0x04,0x1B},{0x07,0x1C,0x07},  // U-Y
    {0x19,0x15,0x13},                                                                       // Z
    {0x0C,0x12,0x1E},{0x1F,0x12,0x0C},{0x1E,0x12,0x12},{0x0C,0x12,0x1F},{0x0C,0x1A,0x14},  // a-e
    {0x04,0x1F,0x05},{0x2E,0x2A,0x1E},{0x1F,0x02,0x1C},{0x00,0x1D,0x00},{0x20,0x1D,0x00},  // f-j
    {0x1F,0x04,0x1A},{0x01,0x1F,0x00},{0x1E,0x04,0x1E},{0x1E,0x02,0x1E},{0x1E,0x12,0x1E},  // k-o
    {0x3E,0x12,0x0C},{0x0C,0x12,0x3E},{0x1E,0x02,0x06},{0x14,0x12,0x0A},{0x02,0x0F,0x12},  // p-t
    {0x1E,0x10,0x1E},{0x0E,0x10,0x0E},{0x1E,0x08,0x1E},{0x1A,0x04,0x1A},{0x2E,0x28,0x1E},  // u-y
    {0x1A,0x12,0x16},                                                                       // z
    {0x1F,0x11,0x1F},{0x12,0x1F,0x10},{0x1D,0x15,0x17},{0x11,0x15,0x1F},{0x07,0x04,0x1F},  // 0-4
    {0x17,0x15,0x1D},{0x1F,0x15,0x1D},{0x01,0x01,0x1F},{0x1F,0x15,0x1F},{0x17,0x15,0x1F},  // 5-9
    {0x00,0x17,0x00},{0x00,0x10,0x00},{0x04,0x04,0x04},{0x04,0x0E,0x04},{0x02,0x29,0x06},  // ! . - + ?
    {0x0A,0x00,0x00},                                                                       // :
    {0x12,0x1F,0x09},{0x20,0x10,0x00},{0x18,0x06,0x01},{0x00,0x03,0x00},{0x0A,0x04,0x0A},  // $ , / ' *
    {0x00,0x0E,0x11},{0x11,0x0E,0x00},{0x04,0x0A,0x11},{0x11,0x0A,0x04},{0x0A,0x0A,0x0A},  // ( ) < > =
    {0x19,0x04,0x13},{0x1F,0x0A,0x1F},                                                      // % #
};

// ASCII 32..122 -> FONT35 index, -1 = no glyph.
static const int8_t IDX35[91] = {
    -1, 62, -1, 79, 68, 78, -1, 71, 73, 74, 72, 65, 69, 64, 63, 70,   // space ! " # $ % & ' ( ) * + , - . /
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 67, -1, 75, 77, 76, 66,   // 0-9 : ; < = > ?
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,            // @ A-O
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,  // P-Z and five symbols
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  // backtick a-o
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,                       // p-z
};

int glyph35(char ch) { return (ch >= 32 && ch <= 122) ? IDX35[ch - 32] : -1; }

// Column-major glyph, bit 0 = top row: the layout of both PPOT's font and
// CHGfx's built-in one.
RAMFUNC(glyph) void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c) {
    c &= 0x0F;
    for (uint8_t i = 0; i < ncols; i++, x++) {
        uint8_t bits = cols[i];
        if (!bits || (unsigned)x >= GFX_W) continue;
        int yy = y;
        uint8_t *p = gfx_fb + yy * GFX_FB_STRIDE + (x >> 1);
        for (; bits; bits >>= 1, yy++, p += GFX_FB_STRIDE)
            if ((bits & 1) && (unsigned)yy < GFX_H) plot(p, x, c);
    }
}

RAMFUNC(text35) int text35(int x, int y, const char *str, uint8_t c) {
    int x0 = x;
    for (; *str; str++) {
        char ch = *str;
        if (ch == '\n') { x = x0; y += 7; continue; }
        if (ch == '~') { x += 2; continue; }
        int g = (ch >= 32 && ch <= 122) ? IDX35[ch - 32] : -1;
        if (g >= 0) glyph(x, y, FONT35[g], 3, c);
        x += 4;
    }
    return x - x0;
}

int text35Width(const char *str) {
    int w = 0, best = 0;
    for (; *str; str++) {
        if (*str == '\n') { if (w > best) best = w; w = 0; }
        else w += (*str == '~') ? 2 : 4;
    }
    if (w > best) best = w;
    return best ? best - 1 : 0;
}

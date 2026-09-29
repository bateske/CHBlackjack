#pragma GCC optimize("Os")
#include <string.h>
#include "Draw.h"

// Hot loops run from SRAM: from flash (3 wait states, no cache) a per-pixel
// loop costs ~3 us a pixel on this part. Each gets its own section so the
// unused ones are dropped at link time.
#define RAMFUNC(name) __attribute__((section(".srodata.ramfunc." #name), noinline))

static inline void plot(uint8_t *p, int x, uint8_t c) {
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else       *p = (uint8_t)((*p & 0xF0) | c);
}

// Corner insets per row for radius 1..4 (pixel-art circles, not chamfers).
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

RAMFUNC(blit4) void blit4(const uint8_t *spr, int x, int y, uint8_t w, uint8_t h, int8_t trans, const uint8_t *remap) {
    if (!remap) { gfx_blit(spr, x, y, w, h, trans); return; }
    uint8_t stride = (uint8_t)((w + 1) >> 1);
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= GFX_H) continue;
        const uint8_t *row = spr + j * stride;
        uint8_t *d = gfx_fb + yy * GFX_FB_STRIDE;
        for (int i = 0; i < w; i++) {
            uint8_t b = row[i >> 1];
            uint8_t v = (i & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
            int xx = x + i;
            if (v != (uint8_t)trans && (unsigned)xx < GFX_W) plot(d + (xx >> 1), xx, remap[v]);
        }
    }
}

// Mostly short spans (the dealer is ~330 of them): write those directly and
// hand only the long ones to gfx_hline.
RAMFUNC(span4) void span4(const uint8_t *d, int x, int y, int8_t trans, const uint8_t *remap) {
    uint8_t h = d[1];
    d += 2;
    for (int j = 0; j < h; j++, y++) {
        uint8_t n = *d++;
        int px = x;
        uint8_t *row = gfx_fb + y * GFX_FB_STRIDE;
        bool rowOk = (unsigned)y < GFX_H;
        while (n--) {
            uint8_t b = *d++;
            uint8_t len = (uint8_t)((b >> 4) + 1), c = (uint8_t)(b & 15);
            if (c != (uint8_t)trans && rowOk) {
                if (remap) c = remap[c];
                if (len > 4 || px < 0 || px + len > GFX_W) gfx_hline(px, y, len, c);
                else for (int k = 0; k < len; k++) plot(row + ((px + k) >> 1), px + k, c);
            }
            px += len;
        }
    }
}

// ---------------------------------------------------------------------------
// Shapes and effects
// ---------------------------------------------------------------------------
static int isqrt(int v) {
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 14;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Half-widths per row for the few ellipse sizes in use (bet circle, chips),
// computed once: the integer square roots cost ~10 us a row from flash.
struct EllipseRows { uint8_t rx, ry, dx[16]; };
static EllipseRows ecache[4];

static const uint8_t *ellipseRows(int rx, int ry) {
    for (auto &e : ecache) if (e.rx == rx && e.ry == ry) return e.dx;
    static uint8_t next = 0;
    EllipseRows &e = ecache[next++ & 3];
    e.rx = (uint8_t)rx; e.ry = (uint8_t)ry;
    for (int dy = 0; dy <= ry && dy < 16; dy++) {
        int t = ry * ry - dy * dy + ry / 2;
        e.dx[dy] = (uint8_t)(ry > 0 ? isqrt(rx * rx * t / (ry * ry)) : rx);
    }
    return e.dx;
}

void fillEllipse(int cx, int cy, int rx, int ry, uint8_t c) {
    const uint8_t *dx = ellipseRows(rx, ry);
    for (int dy = -ry; dy <= ry; dy++) {
        int d = dx[dy < 0 ? -dy : dy];
        gfx_hline(cx - d, cy + dy, 2 * d + 1, c);
    }
}

void ellipse(int cx, int cy, int rx, int ry, uint8_t c) {
    const uint8_t *dx = ellipseRows(rx, ry);
    for (int dy = -ry; dy <= ry; dy++) {
        int a = dy < 0 ? -dy : dy;
        int d = dx[a];
        int inner = (a == ry) ? -d - 1 : dx[a + 1];
        int len = d - inner; if (len < 1) len = 1;
        gfx_hline(cx + d - len + 1, cy + dy, len, c);
        gfx_hline(cx - d, cy + dy, len, c);
    }
}

void dither(int x, int y, int w, int h, uint8_t c, uint8_t phase) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;
    uint8_t cc = (uint8_t)(c | (c << 4));
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        uint8_t *row = gfx_fb + yy * GFX_FB_STRIDE;
        // Pixels where (px + yy + phase) is even get the colour.
        uint8_t m = ((yy + phase) & 1) ? 0xF0 : 0x0F;
        int i = x;
        if (i & 1) { if (m == 0xF0) row[i >> 1] = (uint8_t)((row[i >> 1] & 0x0F) | (c << 4)); i++; }
        for (; i + 1 < x + w; i += 2) row[i >> 1] = (uint8_t)((row[i >> 1] & ~m) | (cc & m));
        if (i < x + w && m == 0x0F) row[i >> 1] = (uint8_t)((row[i >> 1] & 0xF0) | c);
    }
}

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

#pragma GCC optimize("Os")
#include <CHGfx.h>
#include "Mask.h"

#include "Draw.h"
#include "../RamFunc.h"

Mask maskBegin(int w, int h) {
    Mask m;
    m.w = (uint8_t)w; m.h = (uint8_t)h;
    m.stride = (uint8_t)((w + 2 + 7) >> 3);
    m.bits = gfx_chunkScratch();
    uint32_t n = (uint32_t)m.stride * (uint32_t)(h + 2);
    for (uint32_t i = 0; i < n; i++) m.bits[i] = 0;
    return m;
}

// A scale x scale block (scale <= 8) with its top-left at (x, y), clipped
// to the mask: per row, one masked OR into at most two bytes.
static void plot(Mask &m, int x, int y, uint8_t scale) {
    int x0 = x + 1, x1 = x0 + scale;                     // margin
    if (x0 < 0) x0 = 0;
    if (x1 > m.w + 2) x1 = m.w + 2;
    if (x0 >= x1) return;
    unsigned v = (((0xFF00u >> (x1 - x0)) & 0xFF) << 8) >> (x0 & 7);
    uint8_t *p = m.bits + (x0 >> 3);
    for (int j = 0; j < scale; j++) {
        unsigned yy = (unsigned)(y + 1 + j);
        if (yy >= (unsigned)(m.h + 2)) continue;
        uint8_t *row = p + yy * m.stride;
        row[0] |= (uint8_t)(v >> 8);
        if (v & 0xFF) row[1] |= (uint8_t)v;
    }
}

int text35WidthScaled(const char *s, uint8_t scale) { return text35Width(s) * scale; }

void maskText35(Mask &m, int x, int y, const char *s, uint8_t scale, const int8_t *dy) {
    for (int k = 0; s[k]; k++) {
        if (s[k] == '~') { x += 2 * scale; continue; }
        int g = glyph35(s[k]);
        int oy = y + (dy ? dy[k] : 0);
        if (g >= 0)
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 6; row++)
                    if (FONT35[g][col] & (1u << row)) plot(m, x + col * scale, oy + row * scale, scale);
        x += 4 * scale;
    }
}

// Mask row j + 1 is bitmap row j moved right by the 1 px margin, a byte at
// a time. The logos' padding bits are zero (tools/assets.py), so a carry
// out of a row's last byte is a real pixel, and then the mask row has a
// byte for it (w a multiple of 8).
void maskBlit1(Mask &m, const uint8_t *bits, uint8_t w, uint8_t h) {
    uint8_t s = (uint8_t)((w + 7) >> 3);
    uint8_t *d = m.bits + m.stride;
    for (int j = 0; j < h; j++, d += m.stride) {
        uint8_t carry = 0;
        for (int b = 0; b < s; b++) {
            uint8_t v = *bits++;
            d[b] |= (uint8_t)(carry | v >> 1);
            carry = (uint8_t)(v << 7);
        }
        if (carry) d[s] |= carry;
    }
}

// Paint the set runs of one mask row (stride bytes, MSB-first) at screen
// row y, where bit 0 of the row is screen column x. From SRAM, skipping
// empty and full bytes whole: this loop is most of a banner's cost.
RAMFUNC(maskruns) static void runs(const uint8_t *row, uint8_t stride, int x, int y, uint8_t c) {
    if ((unsigned)y >= GFX_H) return;
    int n = stride * 8, i = 0, start = -1;
    while (i < n) {
        uint8_t b = row[i >> 3];
        if ((i & 7) == 0) {
            if (b == 0x00) { if (start >= 0) { gfx_hline(x + start, y, i - start, c); start = -1; } i += 8; continue; }
            if (b == 0xFF) { if (start < 0) start = i; i += 8; continue; }
        }
        if (b & (0x80 >> (i & 7))) { if (start < 0) start = i; }
        else if (start >= 0) { gfx_hline(x + start, y, i - start, c); start = -1; }
        i++;
    }
    if (start >= 0) gfx_hline(x + start, y, n - start, c);
}

// Grow row r by one pixel in all 8 directions into out.
static void dilateRow(const Mask &m, int r, uint8_t *out) {
    const uint8_t *row = m.bits + r * m.stride;
    uint8_t v[32];
    for (int b = 0; b < m.stride; b++) {
        uint8_t o = row[b];
        if (r > 0) o |= row[b - m.stride];
        if (r + 1 < m.h + 2) o |= row[b + m.stride];
        v[b] = o;
    }
    for (int b = 0; b < m.stride; b++)                    // and from x+1, x-1
        out[b] = (uint8_t)(v[b] | v[b] << 1 | (b + 1 < m.stride ? v[b + 1] >> 7 : 0)
                                | v[b] >> 1 | (b > 0 ? v[b - 1] << 7 : 0));
}

// One pass, top to bottom. Row r's shadow lands on screen row r + 1, which
// only later rows paint over, so the layers (shadow, outline, fill) stack
// as they would in three whole passes, with one dilation per row.
void maskDraw(const Mask &m, int x, int y, uint8_t fill, int outline, int shadow, const uint8_t *ramp) {
    int rows = m.h + 2;
    int ox = x - 1, oy = y - 1;                          // undo the margin
    uint8_t d[32];
    for (int r = 0; r < rows; r++) {
        const uint8_t *row = m.bits + r * m.stride, *grown = row;
        if (outline >= 0) { dilateRow(m, r, d); grown = d; }
        if (shadow >= 0) runs(grown, m.stride, ox + 1, oy + r + 1, (uint8_t)shadow);
        if (outline >= 0) runs(d, m.stride, ox, oy + r, (uint8_t)outline);
        if (r > 0 && r < rows - 1) runs(row, m.stride, ox, oy + r, ramp ? ramp[r - 1] : fill);
    }
}

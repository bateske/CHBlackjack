// Drawing primitives CHGfx doesn't provide: rounded rects, ellipses,
// coloured column glyphs, remapped 4 bpp sprites, row-span art, dithering
// and in-place colour remaps (shadows, dimming). Framebuffer only.
//
// The rule on this chip: code runs from flash with 3 wait states, so a
// function call per pixel costs ~2-3 us. Everything here is built from
// gfx_hline spans (word stores, from SRAM) or tight byte loops.
#pragma once
#include <stdint.h>
#include <CHGfx.h>

void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c);   // r <= 4
void roundRect(int x, int y, int w, int h, uint8_t r, uint8_t c);

// 4 bpp sprite (CHGfx packing) with an optional colour remap table.
void blit4(const uint8_t *spr, int x, int y, uint8_t w, uint8_t h, int8_t trans, const uint8_t *remap = nullptr);
// Row-span art: w, h, then per row a count and (len-1)<<4|colour bytes;
// colour `trans` is skipped. Every span is one gfx_hline.
void span4(const uint8_t *data, int x, int y, int8_t trans, const uint8_t *remap = nullptr);

void fillEllipse(int cx, int cy, int rx, int ry, uint8_t c);
void ellipse(int cx, int cy, int rx, int ry, uint8_t c);
void dither(int x, int y, int w, int h, uint8_t c, uint8_t phase);      // 50% checker
void remapRect(int x, int y, int w, int h, const uint8_t *remap);       // recolour in place

// Column-major 1 bpp glyph (bit 0 = top row, <= 8 rows), from SRAM.
void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c);

// PPOT's 3x5 font: 4 px advance, '~' = 2 px space, newline = 7 px down.
int  text35(int x, int y, const char *str, uint8_t c);
int  text35Width(const char *str);
extern const uint8_t FONT35[][3];                   // column bytes per glyph
int  glyph35(char ch);                              // index into FONT35, -1 = none

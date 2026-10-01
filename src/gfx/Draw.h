// The drawing the game still does itself: rounded rects, coloured column
// glyphs, PPOT's 3x5 font and an in-place colour remap (dimming). Ellipses,
// dither, span sprites and the screen shake come from CHGfx 1.3.
//
// The rule on this chip: code runs from flash with 3 wait states, so a
// function call per pixel costs ~2-3 us. Everything here is built from
// gfx_hline spans, is a tight byte loop, or runs from SRAM.
#pragma once
#include <stdint.h>
#include <CHGfx.h>

// Rounded rects from a corner table, r <= 4. CHGfx's gfx_fillRoundRect and
// gfx_roundRect are bigger and slower, and differ on a 4 px mid-flip card
// (Draw.cpp).
void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c);
void roundRect(int x, int y, int w, int h, uint8_t r, uint8_t c);
// fillRound in fill, then roundRect in edge: the game's cards and panels.
void panel(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge);

// Recolour in place, pixel = remap[pixel]. Same as gfx_remapRect, which
// costs more SRAM (see Draw.cpp).
void remapRect(int x, int y, int w, int h, const uint8_t *remap);

// Column-major 1 bpp glyph (bit 0 = top row, <= 8 rows), from SRAM.
void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c);

// PPOT's 3x5 font: 4 px advance, '~' = 2 px space, newline = 7 px down.
// CHGfx_Tiny3x5 has the same glyphs as a GFXfont, but takes 952 B against
// 331 B here: moving this text and the banners (Mask.h) onto it and
// gfx_textFx measured 1.5 KB more flash and 384 B more SRAM.
int  text35(int x, int y, const char *str, uint8_t c);
int  text35Width(const char *str);
extern const uint8_t FONT35[][3];                   // column bytes per glyph
int  glyph35(char ch);                              // index into FONT35, -1 = none

#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask and CHGfx)
#include <CHGfx.h>
#include "Palette.h"

namespace pal {

// Authored on the RGB444 grid so 12 bpp output, fades and the simulator agree.
static const uint16_t BASE[16] = {
    0x000,  // INK
    0xFFF,  // WHITE
    0x042,  // FELT_DK
    0x173,  // FELT
    0x4B5,  // FELT_LT
    0xBBC,  // SILVER
    0xE12,  // RED
    0x702,  // WINE
    0xFC2,  // GOLD
    0x741,  // WOOD
    0x26E,  // BLUE
    0x125,  // NAVY
    0xFB8,  // SKIN
    0x6EF,  // CYAN
    0xF0F,  // FX_A
    0xFC2,  // FX_B
};

static const uint16_t THEMES[THEME_COUNT][3] = {
    {0x042, 0x173, 0x4B5},   // classic green
    {0x024, 0x149, 0x48D},   // blue
    {0x401, 0x812, 0xC44},   // red
    {0x203, 0x517, 0x95B},   // purple
};

static const uint16_t RAINBOW[12] = {
    0xF22, 0xF82, 0xFE2, 0x8F2, 0x2F4, 0x2FC, 0x2EF, 0x28F, 0x42F, 0xA2F, 0xF2E, 0xF28,
};

static uint16_t staged[16];
static bool     dirty = true;
static uint8_t  themeIdx = 0, fadeLevel = 16, desat = 0;
static bool     cycling = true;
static uint32_t ticks = 0;
static uint8_t  flashIdx = 0xFF, flashFrames = 0;
static uint16_t flashColor = 0;

void init() {
    for (uint8_t i = 0; i < 16; i++) staged[i] = BASE[i];
    dirty = true;
    commit();
}

void setTheme(uint8_t t) {
    if (t >= THEME_COUNT) t = 0;
    themeIdx = t;
    for (uint8_t i = 0; i < 3; i++) staged[FELT_DK + i] = THEMES[t][i];
    dirty = true;
}
uint8_t theme() { return themeIdx; }

void setFade(uint8_t level)      { if (level > 16) level = 16; if (level != fadeLevel) { fadeLevel = level; dirty = true; } }
uint8_t fade()                   { return fadeLevel; }
void setDesaturate(uint8_t a)    { if (a > 16) a = 16; if (a != desat) { desat = a; dirty = true; } }
void setFx(uint8_t i, uint16_t c){ staged[i] = c; dirty = true; }
void setCycling(bool on)         { cycling = on; }
uint16_t rgb444(uint8_t i)       { return staged[i]; }

void flash(uint8_t index, uint16_t c, uint8_t frames) {
    flashIdx = index; flashColor = c; flashFrames = frames; dirty = true;
}

void tick() {
    ticks++;
    if (cycling) {
        staged[FX_A] = RAINBOW[(ticks / 3) % 12];
        // FX_B: triangle wave GOLD <-> WHITE over 32 frames.
        uint8_t t = ticks & 31; if (t > 15) t = 31 - t;          // 0..15
        uint8_t r = 15, g = (uint8_t)(12 + (t * 3) / 15), b = (uint8_t)(2 + (t * 13) / 15);
        staged[FX_B] = (uint16_t)((r << 8) | (g << 4) | b);
        dirty = true;
    }
    if (flashFrames) { if (--flashFrames == 0) dirty = true; }
}

void resetClock() { ticks = 0; }

static uint16_t to565(uint16_t c) {
    uint16_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    return (uint16_t)((((r << 1) | (r >> 3)) << 11) | (((g << 2) | (g >> 2)) << 5) | ((b << 1) | (b >> 3)));
}

void commit() {
    if (!dirty) return;
    dirty = false;
    uint16_t out[16];
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t c = (i == flashIdx && flashFrames) ? flashColor : staged[i];
        int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
        if (desat) {
            int y = (r * 5 + g * 9 + b * 2) >> 4;
            r += ((y - r) * desat) >> 4; g += ((y - g) * desat) >> 4; b += ((y - b) * desat) >> 4;
        }
        if (fadeLevel < 16) { r = (r * fadeLevel) >> 4; g = (g * fadeLevel) >> 4; b = (b * fadeLevel) >> 4; }
        out[i] = to565((uint16_t)((r << 8) | (g << 4) | b));
    }
    // gfx_pal is what was last committed: only a real change costs the
    // flush a LUT rebuild.
    for (uint8_t i = 0; i < 16; i++)
        if (out[i] != gfx_pal[i]) { gfx_setPalette(out, 16); return; }
}

}  // namespace pal

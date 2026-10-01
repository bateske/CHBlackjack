#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask and CHGfx)
#include <Arduino.h>
#include <string.h>
#include "Save.h"
#include "../game/Round.h"
#include "../RamFunc.h"

namespace save {

static const uint32_t MAGIC = 0x4A424843u;       // "CHBJ"
static const uint16_t VERSION = 1;
static const uint32_t PAGE = 256;
static const uint32_t PAGE_A = 0xF500, PAGE_B = 0xF600;   // metadata page is 0xF700

struct Record {
    uint32_t magic;
    uint16_t version, seq;
    int32_t  purse;
    uint8_t  hasGame, pad[3];
    Options  opt;
    Stats    stats;
    uint32_t crc;
};
static_assert(sizeof(Record) <= PAGE, "save record must fit one flash page");

static uint16_t lastSeq = 0;
static bool broken = false;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static bool valid(const Record *r) {
    return r->magic == MAGIC && r->version == VERSION &&
           r->crc == crc32((const uint8_t *)r, (uint32_t)(sizeof(Record) - 4));
}

#ifndef CHSIM
extern "C" uint32_t _data_lma, _data_vma, _edata;

static uint32_t imageEnd() {
    return (uint32_t)&_data_lma + ((uint32_t)&_edata - (uint32_t)&_data_vma);
}

// Flash controller, mirrored from CH32SerialBoot/bootloader/src/flash.c.
// Must run from SRAM, with interrupts off (the vector table is in flash).
#define CR_STRT 0x00000040u
#define CR_FLOCK 0x00008000u
#define CR_PAGE_PG 0x00010000u
#define CR_PAGE_ER 0x00020000u
#define CR_BUF_LOAD 0x00040000u
#define CR_BUF_RST 0x00080000u
#define SR_BSY 0x00000001u
#define PROG(a) ((a) + 0x08000000u)

RAMFUNC(save) static void pageWrite(uint32_t addr, const uint32_t *w) {
    uint32_t irq;
    __asm volatile("csrr %0, 0x800" : "=r"(irq));
    __asm volatile("csrw 0x800, %0" : : "r"(irq & ~0x88u));
    FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
    FLASH->MODEKEYR = 0x45670123u; FLASH->MODEKEYR = 0xCDEF89ABu;
    FLASH->CTLR |= CR_PAGE_ER;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_ER;
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->CTLR |= CR_BUF_RST;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    for (uint32_t i = 0; i < PAGE / 4; i++) {
        FLASH->CTLR |= CR_PAGE_PG;
        *(volatile uint32_t *)(PROG(addr) + i * 4) = w[i];
        FLASH->CTLR |= CR_BUF_LOAD;
        while (FLASH->STATR & SR_BSY) {}
        FLASH->CTLR &= ~CR_PAGE_PG;
    }
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    FLASH->CTLR |= CR_FLOCK;
    __asm volatile("csrw 0x800, %0" : : "r"(irq));
}

// Two pages when the image leaves room for them, else just the last one.
static bool twoPages() { return imageEnd() <= PAGE_A; }
bool available() { return !broken && imageEnd() <= PAGE_B; }

static const Record *page(uint32_t a) { return (const Record *)a; }

static bool writePage(uint32_t addr, const Record &rec) {
    static uint32_t buf[PAGE / 4];
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, &rec, sizeof rec);
    pageWrite(addr, buf);
    return memcmp((const void *)addr, buf, PAGE) == 0;
}
#else
// Simulator: one in-memory "flash" so save/continue flows can be scripted.
static uint8_t simFlash[2][PAGE];
bool available() { return !broken; }
static bool twoPages() { return true; }
static const Record *page(uint32_t a) { return (const Record *)simFlash[a == PAGE_B]; }
static bool writePage(uint32_t addr, const Record &rec) {
    memset(simFlash[addr == PAGE_B], 0xFF, PAGE);
    memcpy(simFlash[addr == PAGE_B], &rec, sizeof rec);
    return true;
}
#endif

bool load(Round &r, bool &hasGame) {
    hasGame = false;
    if (!available()) return false;
    const Record *a = page(PAGE_A), *b = page(PAGE_B);
    bool va = twoPages() && valid(a), vb = valid(b);
    const Record *best = nullptr;
    if (va && vb) best = (int16_t)(a->seq - b->seq) > 0 ? a : b;
    else if (va) best = a;
    else if (vb) best = b;
    if (!best) return false;
    lastSeq = best->seq;
    r.opt = best->opt;
    r.stats = best->stats;
    hasGame = best->hasGame && best->purse > 0;
    if (hasGame) r.purse = best->purse;
    return true;
}

bool store(const Round &r, bool hasGame) {
    if (!available()) return false;
    Record rec;
    memset(&rec, 0, sizeof rec);
    rec.magic = MAGIC;
    rec.version = VERSION;
    rec.seq = (uint16_t)(lastSeq + 1);
    rec.purse = r.purse;
    rec.hasGame = hasGame ? 1 : 0;
    rec.opt = r.opt;
    rec.stats = r.stats;
    rec.crc = crc32((const uint8_t *)&rec, (uint32_t)(sizeof rec - 4));
    uint32_t addr = ((rec.seq & 1) || !twoPages()) ? PAGE_B : PAGE_A;   // alternate pages
    if (!writePage(addr, rec)) { broken = true; return false; }
    lastSeq = rec.seq;
    return true;
}

}  // namespace save

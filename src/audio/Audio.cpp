// A small sound sequencer for the CHGame piezo.
//
// The CHGameSound library does far more (four-voice synthesis, MOD playback)
// than a card game needs, and costs ~6.5 KB of flash on a 50 KB part. This
// keeps its two data formats - 8-byte effect steps and Playtune scores - and
// plays them with TIM1 channel 2 on PB10 (the library's pin setup), driven by
// the core's 1 kHz SysTick hook (osSystickHandler), so no other timer is used.
// One pin plays one note at a time, so music has two renderings, as in the
// library: Lead plays the melody (channel 0) and lets the other voices fill
// only its real rests; Arpeggio gives the sounding notes 6 ms turns.
// tools/audio/preview.py renders both to WAV from this file.
#pragma GCC optimize("Os")
#include <Arduino.h>
#include "Audio.h"
#include "Music.h"

#ifdef CHSIM
// The simulator is silent: same interface, no hardware.
static bool simMuted = false;
namespace audio {
bool begin(uint8_t) { return true; }
void setMode(uint8_t) {}
void sfx(Sfx) {}
void blip(uint16_t, uint16_t) {}
void music(Song, bool) {}
void stopMusic() {}
void loopMusic(bool) {}
bool musicPlaying() { return false; }
void mute(bool m) { simMuted = m; }
bool muted() { return simMuted; }
void update() {}
void led(Led) {}
}
#else

struct Step { uint16_t hz, endHz, ms; };        // effect step; hz 0 = rest

#define S(hz, end, ms) { (uint16_t)(hz), (uint16_t)(end), (uint16_t)(ms) }
#define REST(ms)       { 0, 0, (uint16_t)(ms) }

static const Step DEAL[]      = { S(3600, 1500, 22) };
static const Step FLIP[]      = { S(2300, 0, 8), REST(5), S(3300, 0, 12) };
static const Step CHIP[]      = { S(3100, 0, 12), REST(9), S(3700, 0, 26) };
static const Step CURSOR[]    = { S(2100, 0, 10) };
static const Step SELECT[]    = { S(1700, 0, 18), S(2600, 0, 30) };
static const Step DENY[]      = { S(900, 650, 70) };
static const Step WIN[]       = { S(2093, 0, 60), S(2637, 0, 60), S(3136, 0, 60), S(4186, 0, 170) };
static const Step BLACKJACK[] = {
    S(1568, 0, 50), S(2093, 0, 50), S(2637, 0, 50), S(3136, 0, 90),
    S(2093, 0, 40), S(2637, 0, 40), S(2093, 0, 40), S(2637, 0, 40),
    S(3136, 0, 40), S(4186, 0, 40), S(3136, 0, 40), S(4186, 0, 40),
    S(2000, 4200, 220) };
static const Step BUST[]      = { S(1600, 950, 110), S(950, 560, 130), S(560, 330, 230) };
static const Step PUSH[]      = { S(1760, 0, 70), REST(40), S(1760, 0, 70) };
static const Step LOSE[]      = { S(1300, 950, 140), S(950, 700, 220) };
static const Step PEEK[]      = { S(1400, 0, 16) };
static const Step SHUFFLE[]   = {
    S(3000, 0, 7), REST(12), S(3400, 0, 7), REST(12), S(3100, 0, 7), REST(12), S(3500, 0, 7), REST(12),
    S(3000, 0, 7), REST(12), S(3400, 0, 7), REST(12), S(3200, 0, 7), REST(12), S(3600, 0, 7), REST(60),
    S(2400, 3800, 120) };
static const Step COIN[]      = { S(2800, 0, 10), S(3700, 0, 28) };
static const Step SPLIT[]     = { S(1500, 3100, 90), REST(20), S(3100, 0, 30) };
static const Step DOUBLE[]    = { S(2000, 0, 30), REST(20), S(2600, 0, 30), REST(20), S(3200, 0, 60) };
static const Step INSURANCE[] = { S(2637, 0, 60), S(3136, 0, 60), S(2637, 0, 60), S(3136, 0, 120) };
static const Step BROKE[]     = { S(1568, 1480, 300), S(1480, 1397, 300), S(1397, 1319, 300), S(1319, 1180, 800) };
static const Step REVEAL[]    = { S(1800, 3000, 60) };
static const Step WHOOSH[]    = { S(1200, 3800, 90) };

struct SfxDef { const Step *steps; uint8_t n, prio; };
#define DEF(a, p) { a, (uint8_t)(sizeof(a) / sizeof(a[0])), p }
static const SfxDef DEFS[(int)Sfx::COUNT] = {
    DEF(DEAL, 1), DEF(FLIP, 1), DEF(CHIP, 1), DEF(CURSOR, 0), DEF(SELECT, 1), DEF(DENY, 1),
    DEF(WIN, 3), DEF(BLACKJACK, 4), DEF(BUST, 3), DEF(PUSH, 3), DEF(LOSE, 3), DEF(PEEK, 1),
    DEF(SHUFFLE, 2), DEF(COIN, 1), DEF(SPLIT, 2), DEF(DOUBLE, 2), DEF(INSURANCE, 3), DEF(BROKE, 4),
    DEF(REVEAL, 2), DEF(WHOOSH, 1),
};

// --- Sequencer state (shared with the 1 kHz interrupt) ----------------------
static volatile const Step *fxSteps = nullptr;
static volatile uint8_t fxN = 0, fxI = 0, fxPrio = 0;
static volatile uint16_t fxT = 0;
static Step blipStep;

static volatile const uint8_t *score = nullptr, *scorePos = nullptr;
static volatile uint16_t scoreWait = 0;
static volatile bool scoreLoops = true;          // honour the score's restart (0xE0)
static volatile uint8_t notes[4];               // MIDI note per channel, 0 = off
static uint8_t arpT = 0, arpCh = 0, leadHold = 0;
static const uint8_t ARP_MS = 6;                // arpeggio: ms per note
// Lead: how long the melody must be silent before other voices fill in -
// just over the 12 ms gap tools/make_music.py leaves between notes.
static const uint8_t LEAD_HOLD_MS = 20;

static bool started = false, isMuted = false, running = false;
static uint8_t mode = 1;
static uint16_t lastHz = 0;
static uint8_t ledPattern = 0;
static uint16_t ledT = 0;

extern "C" volatile uint32_t CFGHR_tmpB;        // GPIOB CFGHR is write-only: go through the shadow

static void pb10(uint32_t nibble) {
    uint32_t v = (CFGHR_tmpB & ~(15u << 8)) | (nibble << 8);
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
}

static void hwInit() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~(7u << 15)) | (1u << 15);   // TIM1 partial remap: CH2 on PB10
    GPIOB->BCR = 1u << 10;
    pb10(11u);                                                // alternate-function push-pull
    TIM1->CTLR1 = 0; TIM1->CTLR2 = 0; TIM1->SMCFGR = 0; TIM1->DMAINTENR = 0;
    TIM1->CCER = 0;
    TIM1->CHCTLR1 = 0x6800;                                   // CH2 PWM1 + preload
    TIM1->CHCTLR2 = 0;
    TIM1->PSC = 47;                                           // 1 MHz
    TIM1->RPTCR = 0;
    TIM1->ATRLR = 999;
    TIM1->CH2CVR = 0;
    TIM1->CNT = 0;
    TIM1->BDTR = 0x8000;                                      // MOE
    TIM1->CCER = 0x10;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    running = false; lastHz = 0;
}

// Effects restart the timer on every change (their sweeps are voiced that
// way). Music passes smooth: a sounding tone changes pitch at the end of its
// current cycle instead (ATRLR and CH2CVR are preloaded), so switching notes
// never clips a cycle - a click on the piezo.
static void tone(uint16_t hz, bool smooth = false) {
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz || isMuted) {
        TIM1->CH2CVR = 0; TIM1->SWEVGR = 1; TIM1->CTLR1 = 0; TIM1->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    if (smooth && running) {
        TIM1->ATRLR = period - 1;
        TIM1->CH2CVR = period / 2;
        return;
    }
    running = true;
    TIM1->CTLR1 = 0;
    TIM1->ATRLR = period - 1;
    TIM1->CH2CVR = period / 2;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    TIM1->CTLR1 = 0x81;
}

// MIDI note -> Hz: the top octave (C8..B8), shifted down.
static uint16_t noteHz(uint8_t n) {
    static const uint16_t TOP[12] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};
    int sh = 9 - n / 12;
    return sh >= 0 ? (uint16_t)(TOP[n % 12] >> sh) : 0;
}

static void scoreTick() {
    if (!scorePos) return;
    if (scoreWait) { scoreWait--; return; }
    for (int guard = 0; guard < 16; guard++) {
        uint8_t b = *scorePos;
        if (b < 0x80) {                          // wait, 2 bytes big-endian ms
            scoreWait = (uint16_t)((b << 8) | scorePos[1]);
            scorePos += 2;
            if (scoreWait) { scoreWait--; return; }
            continue;
        }
        uint8_t cmd = b & 0xF0, ch = b & 3;
        if (cmd == 0x90) { notes[ch] = scorePos[1]; scorePos += 2; }
        else if (cmd == 0x80) { notes[ch] = 0; scorePos += 1; }
        else if (b == 0xE0 && scoreLoops) { scorePos = score; }
        else { scorePos = nullptr; for (auto &n : notes) n = 0; return; }
    }
}

// The music note to sound this millisecond.
static uint16_t musicHz() {
    if (mode == 2) {
        // Lead: the melody wins. Its short gaps between notes stay silent
        // (filling them with the bass for a few ms is what garbles a tune);
        // the other voices only take over when the melody really rests.
        if (notes[0]) { leadHold = LEAD_HOLD_MS; return noteHz(notes[0]); }
        if (leadHold) { leadHold--; return 0; }
        for (uint8_t k = 1; k < 4; k++) if (notes[k]) return noteHz(notes[k]);
        return 0;
    }
    // Arpeggio: every ARP_MS move on to the next channel that is sounding
    // (at once if the current one stops), so voices take even turns.
    if (++arpT >= ARP_MS || !notes[arpCh]) {
        arpT = 0;
        for (uint8_t k = 0; k < 4; k++) {
            arpCh = (arpCh + 1) & 3;
            if (notes[arpCh]) break;
        }
    }
    return notes[arpCh] ? noteHz(notes[arpCh]) : 0;
}

extern "C" void osSystickHandler(void) {
    if (!started) return;
    scoreTick();
    const Step *s = (const Step *)fxSteps;
    if (s) {
        const Step &st = s[fxI];
        uint16_t hz = st.hz;
        if (hz && st.endHz) hz = (uint16_t)(st.hz + ((int32_t)st.endHz - st.hz) * fxT / st.ms);
        if (++fxT >= st.ms) {
            fxT = 0;
            if (++fxI >= fxN) { fxSteps = nullptr; fxPrio = 0; }
        }
        tone(hz);
    } else {
        tone(musicHz(), true);
    }
}

namespace audio {

bool begin(uint8_t m) {
    mode = m;
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    uint32_t v = (CFGHR_tmpB & ~(15u << 4)) | (3u << 4);          // PB9 LED: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
    GPIOB->BCR = 1u << 9;
    if (!m) { started = false; tone(0); return true; }
    if (!started) hwInit();
    started = true;
    return true;
}

void setMode(uint8_t m) {
    if (!m) { started = false; lastHz = 1; tone(0); }
    begin(m);
}

static void play(const Step *st, uint8_t n, uint8_t prio) {
    if (!started || isMuted) return;
    if (fxSteps && prio < fxPrio) return;
    __disable_irq();
    fxSteps = st; fxN = n; fxI = 0; fxT = 0; fxPrio = prio;
    __enable_irq();
}

void sfx(Sfx s) {
    const SfxDef &d = DEFS[(int)s];
    play(d.steps, d.n, d.prio);
}

void blip(uint16_t hz, uint16_t ms) {
    if (fxSteps && fxPrio > 1) return;
    __disable_irq();
    blipStep.hz = hz; blipStep.endHz = 0; blipStep.ms = ms;
    __enable_irq();
    play(&blipStep, 1, 0);
}

void music(Song s, bool loop) {
    const uint8_t *data; size_t n;
    music::get((uint8_t)s, loop, data, n);
    __disable_irq();
    score = data; scorePos = data; scoreWait = 0; scoreLoops = true;
    for (auto &x : notes) x = 0;
    leadHold = 0;
    __enable_irq();
}

void loopMusic(bool on) { scoreLoops = on; }

// With sound off the score is not advanced at all, so it counts as finished.
bool musicPlaying() { return started && scorePos; }

void stopMusic() {
    __disable_irq();
    scorePos = nullptr;
    for (auto &x : notes) x = 0;
    __enable_irq();
}

void mute(bool m) { isMuted = m; lastHz = 1; }
bool muted() { return isMuted; }

void led(Led p) { ledPattern = p; ledT = 0; }

void update() {
    if (!ledPattern) return;
    ledT++;
    bool on = false;
    switch (ledPattern) {
        case LED_BLINK:  on = ledT < 12; if (ledT > 12) ledPattern = 0; break;
        case LED_TRIPLE: on = (ledT % 16) < 8; if (ledT > 48) ledPattern = 0; break;
        case LED_PARTY:  on = (ledT % 8) < 4; if (ledT > 240) ledPattern = 0; break;
    }
    if (on && ledPattern) GPIOB->BSHR = 1u << 9;
    else GPIOB->BCR = 1u << 9;
}

}  // namespace audio
#endif  // CHSIM

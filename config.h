// CHBlackjack build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.2+ with its default Peripherals menu setting
// ("Game"), which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash.
#pragma once

#define CHBJ_VERSION     "1.0"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~1.3 KB and one of the two save pages).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHBJ_DEBUG
#ifdef CHSIM
#define CHBJ_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHBJ_DEBUG       0
#endif
#endif

// Device debug builds carry the ~1.3 KB protocol, so they leave out things
// the tests never need: the music scores and the credits page.
// The simulator (not flash-bound) and release builds keep everything.
#if CHBJ_DEBUG && !defined(CHSIM) && !defined(CHBJ_FULL)
#define CHBJ_LEAN        1
#else
#define CHBJ_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHBJ_PROFILE
#define CHBJ_PROFILE     0
#endif

// Frame rate the game logic is paced for (PPOT ran at 60).
#define CHBJ_FPS         60

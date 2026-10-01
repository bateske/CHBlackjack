// CHBlackjack build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHBJ_VERSION     "1.0"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~1.8 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHBJ_DEBUG
#ifdef CHSIM
#define CHBJ_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHBJ_DEBUG       0
#endif
#endif

// Debug builds carry the ~1.8 KB protocol, so they leave out things the
// tests never need: every CHBJ_DEBUG build, the simulator included, has no
// music scores (src/audio/Music.cpp, from tools/make_music.py), and device
// debug builds also drop the credits page unless built with -DCHBJ_FULL.
// Release builds keep everything.
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

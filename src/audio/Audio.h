// Sound and the status LED.
//
// PPOT's only sound was a blip on its splash screen (a Timer3 square wave);
// here every action has a voice, built on the CHGameSound library. Effects
// are short step lists (8 bytes a step) kept inside the piezo's 1-4 kHz
// sweet spot; music uses the library's Playtune-compatible score format.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Deal, Flip, Chip, Cursor, Select, Deny, Win, Blackjack, Bust, Push, Lose,
    Peek, Shuffle, Coin, Split, Double, Insurance, Broke, Reveal, Whoosh, COUNT
};
enum class Song : uint8_t { Title, Victory, Broke };

namespace audio {

bool begin(uint8_t mode);           // 0 off, 1 arpeggio, 2 lead
void setMode(uint8_t mode);
void sfx(Sfx s);
void blip(uint16_t hz, uint16_t ms);     // typewriter, ticks
void music(Song s, bool loop);
void stopMusic();
void loopMusic(bool on);            // off: the tune stops at its end instead of repeating
bool musicPlaying();
void mute(bool m);
bool muted();
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins and blackjacks.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio

// Saving the purse, options and lifetime stats.
//
// The Arduboy keeps these in EEPROM. CHGame has no EEPROM, but its
// bootloader only erases the flash pages a new sketch occupies, so the last
// pages of the application region survive re-uploads (verified on hardware
// with tools/probes/FlashProbe). Two pages are used in turn, each record
// carrying a sequence number and a CRC, so a power cut mid-write can only
// lose the newest save. If this sketch ever grows into those pages, saving
// switches itself off rather than overwrite code.
#pragma once
#include <stdint.h>

class Round;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Round &r, bool &hasGame); // options + stats always; purse if hasGame
bool store(const Round &r, bool hasGame);

}  // namespace save

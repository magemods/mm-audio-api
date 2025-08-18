#ifndef __RECOMP_SOUNDFONT__
#define __RECOMP_SOUNDFONT__

#include <global.h>

typedef struct CustomSoundFont {
    u16 numInstruments;
    u16 numDrums;
    u16 numSfx;
    u8 instrumentsCapacity;
    u8 drumsCapacity;
    u16 sfxCapacity;
    uintptr_t origRomAddr;
    Drum** drums __attribute__((aligned(16)));
    SoundEffect* soundEffects;
    Instrument* instruments[];
} CustomSoundFont;

#endif

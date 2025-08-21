#ifndef __AUDIO_API_SOUNDFONT_H__
#define __AUDIO_API_SOUNDFONT_H__

/*! \file audio_api/soundfont.h
    \version 0.5.0
    \brief Low level imports for working with soundfonts
 */

#include "audio/soundfont_file.h"

static EnvelopePoint DefaultEnvelopePoint[] = {
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(32700, 32700),
    ENVELOPE_HANG(),
};

typedef enum SoundFontType : u8 {
    SOUNDFONT_VANILLA = 0,
    SOUNDFONT_CUSTOM,
} SoundFontType;

typedef struct CustomSoundFont {
    SoundFontType type;
    u16 sampleBank1;
    u16 sampleBank2;
    u8 numInstruments;
    u8 numDrums;
    u16 numSfx;
    u8 instrumentsCapacity;
    u8 drumsCapacity;
    u16 sfxCapacity;
    Instrument** instruments;
    Drum** drums;
    SoundEffect* soundEffects;
} CustomSoundFont;

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_CreateEmptySoundFont());
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_ImportVanillaSoundFont(uintptr_t* fontData, u8 sampleBank1, u8 sampleBank2, u8 numInstruments, u8 numDrums, u16 numSfx));

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddSoundFont(AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_ReplaceSoundFont(s32 fontId, AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_RestoreSoundFont(s32 fontId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_SetSoundFontSampleBank(s32 fontId, s32 bankNum, s32 bankId));

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddInstrument(s32 fontId, Instrument* instrument));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddDrum(s32 fontId, Drum* drum));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddSoundEffect(s32 fontId, SoundEffect* sfx));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceDrum(s32 fontId, s32 drumId, Drum* drum));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceSoundEffect(s32 fontId, s32 sfxId, SoundEffect* sfx));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceInstrument(s32 fontId, s32 instId, Instrument* instrument));

#endif

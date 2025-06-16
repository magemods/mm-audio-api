#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "util.h"
#include "audio/soundfont_file.h"

INCBIN(sequence, "src/test/DetectiveSky612-DanceOfDeath.zseq");
INCBIN(rickroll, "src/test/rickroll32kHz.raw");
INCBIN(attack1, "src/test/attack1.raw");
INCBIN(attack2, "src/test/attack2.raw");
INCBIN(attack3, "src/test/attack3.raw");

RECOMP_IMPORT(".", s16 AudioApi_AddSequence(AudioTableEntry entry));
RECOMP_IMPORT(".", void AudioApi_ReplaceSequence(s16 id, AudioTableEntry entry));
RECOMP_IMPORT(".", void AudioApi_RestoreSequence(s16 id));
RECOMP_IMPORT(".", void AudioApi_ReplaceInstrument(s32 id, Instrument* instrument));
RECOMP_IMPORT(".", void AudioApi_ReplaceSoundEffect(s32 id, SoundEffect* sfx));

ALIGNED(16) EnvelopePoint myEnv[] = {
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(32700, 32700),
    ENVELOPE_HANG(),
};

RECOMP_CALLBACK(".", AudioApi_Init) void my_audio_api_init() {
    AudioTableEntry mySeq = {
        (uintptr_t) sequence,    // romAddr
        sequence_end - sequence, // size
        MEDIUM_CART,             // medium
        CACHE_EITHER,            // cachePolicy
        0, 0, 0,                 // shortData
    };
    AudioApi_ReplaceSequence(NA_BGM_FILE_SELECT, mySeq);

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (rickroll_end - rickroll) / 2, 2, 0 }, {}
        };

        Sample mySample = {
            0, CODEC_S16, MEDIUM_CART, false, false,
            rickroll_end - rickroll,
            rickroll,
            &mySample_LOOP,
            NULL
        };

        Instrument myInstrument = {
            false,
            INSTR_SAMPLE_LO_NONE,
            INSTR_SAMPLE_HI_NONE,
            251,
            myEnv,
            INSTR_SAMPLE_NONE,
            { &mySample, 2.0f },
            INSTR_SAMPLE_NONE,
        };

        AudioApi_ReplaceInstrument(61, &myInstrument);
    }

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (attack1_end - attack1) / 2, 2, 0 }, {}
        };

        Sample mySample = {
            0, CODEC_S16, MEDIUM_CART, false, false,
            attack1_end - attack1,
            attack1,
            &mySample_LOOP,
            NULL
        };

        SoundEffect mySfx = {
            { &mySample, 2.0f },
        };

        recomp_printf("CHAN: %d\n", NA_SE_VO_LI_SWORD_N);

        AudioApi_ReplaceSoundEffect(32, &mySfx); // does nothing?
        //AudioApi_ReplaceSoundEffect(196, &mySfx); // segfaults on launch
        //AudioApi_ReplaceSoundEffect(224, &mySfx); // works
    }

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (attack2_end - attack2) / 2, 2, 0 }, {}
        };

        Sample mySample = {
            0, CODEC_S16, MEDIUM_CART, false, false,
            attack2_end - attack2,
            attack2,
            &mySample_LOOP,
            NULL
        };

        SoundEffect mySfx = {
            { &mySample, 2.0f },
        };

        // AudioApi_ReplaceSoundEffect(33, &mySfx); // does nothing?
        // AudioApi_ReplaceSoundEffect(197, &mySfx); // segfault on launch
        // AudioApi_ReplaceSoundEffect(225, &mySfx); // segfault on attack
    }
}

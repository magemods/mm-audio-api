#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "util.h"
#include "audio/soundfont_file.h"

INCBIN(sequence, "src/test/DetectiveSky612-DanceOfDeath.zseq");
INCBIN(rickroll, "src/test/rickroll32kHzmono.raw");
INCBIN(attack1, "src/test/attack1.raw");
INCBIN(attack2, "src/test/attack2.raw");
INCBIN(attack3, "src/test/attack3.raw");

RECOMP_IMPORT(".", s16 AudioApi_AddSequence(AudioTableEntry* entry));
RECOMP_IMPORT(".", void AudioApi_ReplaceSequence(AudioTableEntry* entry, s32 seqId));
RECOMP_IMPORT(".", void AudioApi_RestoreSequence(s32 seqId));
RECOMP_IMPORT(".", void AudioApi_SetSequenceFontId(s32 seqId, s32 fontNum, s32 fontId));
RECOMP_IMPORT(".", void AudioApi_SetSequenceFlags(s32 seqId, u8 flags));
RECOMP_IMPORT(".", void AudioApi_ReplaceSoundEffect(SoundEffect* sfx, s32 sfxId));
RECOMP_IMPORT(".", void AudioApi_ReplaceInstrument(Instrument* instrument, s32 instId));

EnvelopePoint myEnv[] = {
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(32700, 32700),
    ENVELOPE_HANG(),
};

RECOMP_CALLBACK(".", AudioApi_onInit) void my_mod_on_init() {

    {
        // Replace the file select sequence with our own custom one

        AudioTableEntry mySeq = {
            (uintptr_t) sequence,    // romAddr
            sequence_end - sequence, // size
            MEDIUM_CART,             // medium
            CACHE_EITHER,            // cachePolicy
            0, 0, 0,                 // shortData
        };

        AudioApi_ReplaceSequence(&mySeq, NA_BGM_FILE_SELECT);
        AudioApi_SetSequenceFontId(NA_BGM_FILE_SELECT, 0, 3);

        // Also add as a new sequence (seqId = 128)
        s32 newSeqId = AudioApi_AddSequence(&mySeq);
        AudioApi_SetSequenceFontId(newSeqId, 0, 3);
    }

    {
        // Replace the cucco instrument with one that plays the intro to Never Going To Give You Up

        AdpcmLoop mySample_LOOP = {
            { 0, (rickroll_end - rickroll) / 2, 0, 0 }, {}
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
            { &mySample, 1.0f },
            INSTR_SAMPLE_NONE,
        };

        AudioApi_ReplaceInstrument(&myInstrument, 61);
    }

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (attack1_end - attack1) / 2, 0, 0 }, {}
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

        AudioApi_ReplaceSoundEffect(&mySfx, 28);

        mySfx.tunedSample.tuning = 2.05f;
        AudioApi_ReplaceSoundEffect(&mySfx, 30);

        mySfx.tunedSample.tuning = 2.1f;
        AudioApi_ReplaceSoundEffect(&mySfx, 32);
    }

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (attack2_end - attack2) / 2, 0, 0 }, {}
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

        AudioApi_ReplaceSoundEffect(&mySfx, 29);

        mySfx.tunedSample.tuning = 2.1f;
        AudioApi_ReplaceSoundEffect(&mySfx, 31);
    }

    {
        AdpcmLoop mySample_LOOP = {
            { 0, (attack3_end - attack3) / 2, 0, 0 }, {}
        };

        Sample mySample = {
            0, CODEC_S16, MEDIUM_CART, false, false,
            attack3_end - attack3,
            attack3,
            &mySample_LOOP,
            NULL
        };

        SoundEffect mySfx = {
            { &mySample, 2.0f },
        };

        AudioApi_ReplaceSoundEffect(&mySfx, 33);
    }
}

RECOMP_CALLBACK(".", AudioApi_onLoadSequence) void my_mod_on_load_sequence(u8* ramAddr, s32 seqId) {
    // Here, we can modify sequence 0 in various ways.
    if (seqId == 0) {

        u16* tableEnvironmentPtr = (u16*)(ramAddr + 0x23E8);

        // ----------------------------------------

        // For instruments, if we update the sample, we must also update the corresponding
        // channel's NOTEDV delay (length), otherwise it will cut off early
        u8* chanCuccoPtr = (u8*)(ramAddr + 0x2938);
        u16 delay = 864; // num_seconds * 96 ??

        // print_bytes(chanCuccoPtr, 11);
        // C1 - ASEQ_OP_CHAN_INSTR
        // 3D - instr 61
        // 88 - ASEQ_OP_CHAN_LDLAYER 0x88 + <layerNum:b3>
        // 29 - LAYER_293E
        // 3E
        // FF - ASEQ_OP_END
        // 67 - ASEQ_OP_LAYER_NOTEDV  0x40 + 39
        // 80 - <delay:var>
        // A6
        // 69 - <velocity:u8>
        // FF - ASEQ_OP_END

        // Write Long encoded numbers a la MIDI
        chanCuccoPtr[7] = 0x80 | (delay & 0x7f00) >> 8;
        chanCuccoPtr[8] = delay & 0xFF;

        // ----------------------------------------

        // Change CHAN_EV_SMALL_DOG_BARK's layer to CHAN_EV_RUPY_FALL's layer, which is 12 bytes ahead
        u8* chanDogBarkPtr = (u8*)(ramAddr + 0x3D0D);
        chanDogBarkPtr[2] += 0xC;
    }
}

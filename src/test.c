#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "util.h"
#include "audio/soundfont_file.h"

INCBIN(sequence, "src/test/DetectiveSky612-DanceOfDeath.zseq");
INCBIN(rickroll, "src/test/rickroll32kHzmono.raw");
INCBIN(drum,    "src/test/drum.raw");
INCBIN(attack1, "src/test/attack1.raw");
INCBIN(attack2, "src/test/attack2.raw");
INCBIN(attack3, "src/test/attack3.raw");

RECOMP_IMPORT(".", void AudioApi_StartSequence(u8 seqPlayerIndex, s32 seqId, u16 seqArgs, u16 fadeInDuration));
RECOMP_IMPORT(".", s32 AudioApi_AddSequence(AudioTableEntry* entry));
RECOMP_IMPORT(".", void AudioApi_ReplaceSequence(s32 seqId, AudioTableEntry* entry));
RECOMP_IMPORT(".", void AudioApi_RestoreSequence(s32 seqId));
RECOMP_IMPORT(".", s32 AudioApi_AddSequenceFont(s32 seqId, s32 fontId));
RECOMP_IMPORT(".", void AudioApi_ReplaceSequenceFont(s32 seqId, s32 fontNum, s32 fontId));
RECOMP_IMPORT(".", void AudioApi_RestoreSequenceFont(s32 seqId, s32 fontNum));
RECOMP_IMPORT(".", void AudioApi_SetSequenceFlags(s32 seqId, u8 flags));
RECOMP_IMPORT(".", void AudioApi_RestoreSequenceFlags(s32 seqId));
RECOMP_IMPORT(".", void AudioApi_ReplaceDrum(s32 fontId, s32 drumId, Drum* drum));
RECOMP_IMPORT(".", void AudioApi_ReplaceSoundEffect(s32 fontId, s32 sfxId, SoundEffect* sfx));
RECOMP_IMPORT(".", void AudioApi_ReplaceInstrument(s32 fontId, s32 instId, Instrument* instrument));

EnvelopePoint myEnv[] = {
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(    1, 32700),
    ENVELOPE_POINT(32700, 32700),
    ENVELOPE_HANG(),
};

EnvelopePoint drumEnv[] = {
    ENVELOPE_POINT(    2, 32700),
    ENVELOPE_POINT(  298,     0),
    ENVELOPE_POINT(    1,     0),
    ENVELOPE_HANG(),
};


s32 newSeqId;

RECOMP_HOOK("Player_Update") void onPlayer_Update(Player* this, PlayState* play) {
    if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_L)) {
        AudioApi_StartSequence(SEQ_PLAYER_BGM_MAIN, newSeqId, 0, 0);
    }
}

RECOMP_CALLBACK(".", AudioApi_Init) void my_mod_on_init() {

    {
        // Replace the file select sequence with our own custom one

        AudioTableEntry mySeq = {
            (uintptr_t) sequence,    // romAddr
            sequence_end - sequence, // size
            MEDIUM_CART,             // medium
            CACHE_EITHER,            // cachePolicy
            0, 0, 0,                 // shortData
        };

        AudioApi_ReplaceSequence(NA_BGM_FILE_SELECT, &mySeq);
        AudioApi_ReplaceSequenceFont(NA_BGM_FILE_SELECT, 0, 0x03);

        // Also add as a new sequence (seqId = 128)
        newSeqId = AudioApi_AddSequence(&mySeq);
        AudioApi_AddSequenceFont(newSeqId, 0x03);
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

        AudioApi_ReplaceInstrument(0x00, 61, &myInstrument);
    }

    {
        // Replace Timpani

        AdpcmLoop mySample_LOOP = {
            { 0, (drum_end - drum) / 2, 0, 0 }, {}
        };

        Sample mySample = {
            0, CODEC_S16, MEDIUM_CART, false, false,
            drum_end - drum,
            drum,
            &mySample_LOOP,
            NULL
        };

        Drum myDrum = {
            251, 74, false,
            { &mySample, 1.0f },
            drumEnv,
        };

        for (s32 i = 33; i < 64; i++) {
            // Scale tuning from 0.45f - 2.0f
            myDrum.tunedSample.tuning = 0.05f + 0.05f * (i - 33);
            AudioApi_ReplaceDrum(0x03, i, &myDrum);
        }
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

        AudioApi_ReplaceSoundEffect(0x00, 28, &mySfx);

        mySfx.tunedSample.tuning = 2.05f;
        AudioApi_ReplaceSoundEffect(0x00, 30, &mySfx);

        mySfx.tunedSample.tuning = 2.1f;
        AudioApi_ReplaceSoundEffect(0x00, 32, &mySfx);
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

        AudioApi_ReplaceSoundEffect(0x00, 29, &mySfx);

        mySfx.tunedSample.tuning = 2.1f;
        AudioApi_ReplaceSoundEffect(0x00, 31, &mySfx);
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

        AudioApi_ReplaceSoundEffect(0x00, 33, &mySfx);
    }
}

RECOMP_CALLBACK(".", AudioApi_Ready) void my_mod_on_ready() {
}

RECOMP_CALLBACK(".", AudioApi_SequenceLoaded) void my_mod_on_load_sequence(s32 seqId, u8* ramAddr) {
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

#include <core/sequence_functions.h>
#include <global.h>
#include <recomp/modding.h>
#include <core/audio_cmd.h>

#include <recomp/recomputils.h>

/**
 * This file patches various functions found in sequence.c and code_8019AF00.c in order to support
 * more than 256 sequences, as well as provide new exported functions for the same purpose.
 *
 * The main issue is that sequence IDs are either stored as u8, or u16. In the latter case, this is
 * actually `(seqArgs << 8) | seqId`, which means we are still limited to 8 bits for the seqId.
 *
 * All of the newly exported functions have seqId and seqArgs as separate parameters, otherwise the
 * function signature should be the same as the vanilla functions, which have been patched to use
 * the newly defined functions. These vanilla functions will continue to work with seqId being the
 * bits for seqId and seqArgs combined.
 *
 * One important note is that using `AudioSeq_GetActiveSeqId()` when a custom sequence is playing
 * will return the value 0xFE. This represents a sequence unknown to the vanilla game, since we
 * cannot return the actual value from this function. Instead, you should use the following calls:
 * `AudioApi_GetActiveSeqId()` and `AudioApi_GetActiveSeqArgs()`.
 */

#define SEQ_SCREEN_WEIGHTED_DIST(projectedPos) \
    (sqrtf(SQ((projectedPos)->z) + ((SQ((projectedPos)->x) / 4.0f) + (SQ((projectedPos)->y) / 6.0f))))

#define SEQ_FLAG_ENEMY (1 << 0)
#define SEQ_FLAG_FANFARE (1 << 1)
#define SEQ_FLAG_FANFARE_KAMARO (1 << 2)
#define SEQ_FLAG_RESTORE (1 << 3)
#define SEQ_FLAG_RESUME (1 << 4)
#define SEQ_FLAG_RESUME_PREV (1 << 5)
#define SEQ_FLAG_SKIP_HARP_INTRO (1 << 6)
#define SEQ_FLAG_NO_AMBIENCE (1 << 7)
#define SEQ_RESUME_POINT_NONE 0xC0
#define AMBIENCE_CHANNEL_PROPERTIES_ENTRIES_MAX 33

typedef enum {
    /* 0x0 */ SEQ_PLAYER_IO_PORT_0,
    /* 0x1 */ SEQ_PLAYER_IO_PORT_1,
    /* 0x2 */ SEQ_PLAYER_IO_PORT_2,
    /* 0x3 */ SEQ_PLAYER_IO_PORT_3,
    /* 0x4 */ SEQ_PLAYER_IO_PORT_4,
    /* 0x5 */ SEQ_PLAYER_IO_PORT_5,
    /* 0x6 */ SEQ_PLAYER_IO_PORT_6,
    /* 0x7 */ SEQ_PLAYER_IO_PORT_7
} SeqPlayerIOPort;

typedef enum {
    /* 0 */ AUDIO_PAUSE_STATE_CLOSED,
    /* 1 */ AUDIO_PAUSE_STATE_CLOSING,
    /* 2 */ AUDIO_PAUSE_STATE_OPEN
} AudioPauseState;

typedef struct {
    /* 0x0 */ u16 initChannelMask;     // bitwise flag for 16 channels, channels to initialize
    /* 0x2 */ u16 initMuteChannelMask; // bitwise flag for 16 channels, channels to mute upon initialization
    /* 0x4 */ u8 channelProperties[3 * AMBIENCE_CHANNEL_PROPERTIES_ENTRIES_MAX + 1];
} AmbienceDataIO;

extern AmbienceDataIO sAmbienceData[20];

extern u8 sAudioPauseState;
extern u8 sSpatialSeqIsActive[4];
extern u8 sIsFinalHoursOrSoaring;
extern u8 sObjSoundFanfareRequested;
extern Vec3f sObjSoundFanfarePos;
extern Vec3f sSpatialSeqNoFilterPos;
extern Vec3f sSpatialSeqFilterPos;
extern f32 sSpatialSeqMaxDist;
extern u8 sSpatialSeqFlags;
extern u8 sSpatialSeqPlayerIndex;
extern u8 sSpatialSeqFadeTimer;
extern u8 sSpatialSubBgmFadeTimer;

// Sequence bss
extern u8 sRomaniSingingTimer;
extern u8 sFanfareState;
extern u8 sAllPlayersMutedExceptSystemAndOcarina;

// Sequence Data
extern u8 sSeqFlags[];
extern u8 sPrevSeqMode;
extern f32 sBgmEnemyDist;
extern s8 sBgmEnemyVolume;
extern u8 sSeqResumePoint;
extern u32 sNumFramesStill;
extern u32 sNumFramesMoving;
extern u8 sAudioExtraFilter;
extern u8 sAudioExtraFilter2;

// System Data
extern s8 sSoundMode;
extern s8 sAudioCutsceneFlag;

extern void Audio_MuteBgmPlayersForFanfare(void);
extern void Audio_StopSequenceAtPos(u8 seqPlayerIndex, u8 volumeFadeTimer);
extern void Audio_SplitBgmChannels(s8 volumeSplit);

extern void Audio_SetObjSoundProperties(u8 seqPlayerIndex, Vec3f* pos, s16 flags,
                                        f32 minDist, f32 maxDist, f32 maxVolume, f32 minVolume);

extern void Audio_SetSequenceProperties(u8 seqPlayerIndex, Vec3f* projectedPos, s16 flags,
                                        f32 minDist, f32 maxDist, f32 maxVolume, f32 minVolume);

void AudioApi_PlaySequenceWithSeqPlayerIO(s8 seqPlayerIndex, s32 seqId, u16 seqArgs,
                                          u8 fadeInDuration, s8 ioPort, u8 ioData);


SeqRequestExtended sExtSeqRequests[SEQ_PLAYER_MAX][5];
ActiveSequenceExtended gExtActiveSeqs[SEQ_PLAYER_MAX];
u8* sExtSeqFlags = sSeqFlags;

s32 sExtRequestedSceneSeqId;
s32 sExtFanfareSeqId;
s32 sExtObjSoundFanfareSeqId;
s32 sExtSpatialSeqSeqId;
s32 sExtPrevAmbienceSeqId;
s32 sExtPrevMainBgmSeqId      = NA_BGM_DISABLED;
s32 sExtPrevSceneSeqId        = NA_BGM_GENERAL_SFX;
s32 sExtObjSoundMainBgmSeqId  = NA_BGM_GENERAL_SFX;

u16 sExtRequestedSceneSeqArgs = 0x0000;
u16 sExtFanfareSeqArgs        = 0x0000;
u16 sExtPrevAmbienceSeqArgs   = 0x0000;
u16 sExtPrevMainBgmSeqArgs    = 0x0000;

RECOMP_DECLARE_EVENT(AudioApi_SequenceStarted(s8 seqPlayerIndex, s32 seqId, u16 seqArgs, u16 fadeInDuration));

// ======== MAIN START, STOP, GETTER, SETTER FUNCTIONS ========

RECOMP_EXPORT void AudioApi_StartSequence(u8 seqPlayerIndex, s32 seqId, u16 seqArgs, u16 fadeInDuration) {
    u8 channelIndex;
    u16 skipTicks;
    s32 pad;

    // For compatibility with other functions, allow seqArgs to be either in range of 0xFF00 or 0xFF
    if (seqArgs > 0xFF) {
        seqArgs >>= 8;
    }

    if (!sStartSeqDisabled || (seqPlayerIndex == SEQ_PLAYER_SFX)) {
        seqArgs &= 0x7F;
        if (seqArgs == 0x7F) {
            // fadeInDuration interpreted as seconds, 60 is refresh rate and does not account for PAL
            skipTicks = (fadeInDuration >> 3) * 60 * gAudioCtx.audioBufferParameters.updatesPerFrame;
            AUDIOCMD_EXTENDED_GLOBAL_INIT_SEQPLAYER_SKIP_TICKS(seqPlayerIndex, seqId, skipTicks);
        } else {
            // fadeInDuration interpreted as 1/30th of a second, does not account for change in refresh rate for PAL
            fadeInDuration = (fadeInDuration * (u16)gAudioCtx.audioBufferParameters.updatesPerFrame) / 4;
            AUDIOCMD_EXTENDED_GLOBAL_INIT_SEQPLAYER(seqPlayerIndex, seqId, fadeInDuration);
        }

        gExtActiveSeqs[seqPlayerIndex].seqId = seqId;
        gExtActiveSeqs[seqPlayerIndex].seqArgs = seqArgs << 8;
        gExtActiveSeqs[seqPlayerIndex].prevSeqId = seqId;
        gExtActiveSeqs[seqPlayerIndex].prevSeqArgs = seqArgs << 8;
        gActiveSeqs[seqPlayerIndex].isSeqPlayerInit = true;

        if (gActiveSeqs[seqPlayerIndex].volCur != 1.0f) {
            AUDIOCMD_SEQPLAYER_FADE_VOLUME_SCALE(seqPlayerIndex, gActiveSeqs[seqPlayerIndex].volCur);
        }

        gActiveSeqs[seqPlayerIndex].tempoTimer = 0;
        gActiveSeqs[seqPlayerIndex].tempoOriginal = 0;
        gActiveSeqs[seqPlayerIndex].tempoCmd = 0;

        for (channelIndex = 0; channelIndex < SEQ_NUM_CHANNELS; channelIndex++) {
            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volCur = 1.0f;
            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volTimer = 0;
            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleCur = 1.0f;
            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleTimer = 0;
        }

        gActiveSeqs[seqPlayerIndex].freqScaleChannelFlags = 0;
        gActiveSeqs[seqPlayerIndex].volChannelFlags = 0;

        // @mod call event
        AudioApi_SequenceStarted(seqPlayerIndex, seqId, seqArgs, fadeInDuration);
    }
}

RECOMP_PATCH void AudioSeq_StartSequence(u8 seqPlayerIndex, u8 seqId, u8 seqArgs, u16 fadeInDuration) {
    AudioApi_StartSequence(seqPlayerIndex, seqId, seqArgs, fadeInDuration);
}

RECOMP_PATCH void AudioSeq_StopSequence(u8 seqPlayerIndex, u16 fadeOutDuration) {
    fadeOutDuration = (fadeOutDuration * (u16)gAudioCtx.audioBufferParameters.updatesPerFrame) / 4;
    AUDIOCMD_GLOBAL_DISABLE_SEQPLAYER(seqPlayerIndex, fadeOutDuration);
    gExtActiveSeqs[seqPlayerIndex].seqId = NA_BGM_DISABLED;
    gExtActiveSeqs[seqPlayerIndex].seqArgs = 0x0000;
}

RECOMP_EXPORT s32 AudioApi_GetActiveSeqId(u8 seqPlayerIndex) {
    if (gActiveSeqs[seqPlayerIndex].isWaitingForFonts == true) {
        return gExtActiveSeqs[seqPlayerIndex].startAsyncSeqCmd.asInt;
    }
    return gExtActiveSeqs[seqPlayerIndex].seqId;
}

RECOMP_EXPORT u16 AudioApi_GetActiveSeqArgs(u8 seqPlayerIndex) {
    if (gActiveSeqs[seqPlayerIndex].isWaitingForFonts == true) {
        return gExtActiveSeqs[seqPlayerIndex].startAsyncSeqCmd.arg0 & 0xFF00;
    }
    return gExtActiveSeqs[seqPlayerIndex].seqArgs;
}

RECOMP_PATCH u16 AudioSeq_GetActiveSeqId(u8 seqPlayerIndex) {
    s32 seqId = AudioApi_GetActiveSeqId(seqPlayerIndex);
    u16 seqArgs = AudioApi_GetActiveSeqArgs(seqPlayerIndex);

    if (seqId == NA_BGM_DISABLED) {
        return NA_BGM_DISABLED;
    }
    if (seqId < 0x100) {
        return seqId | seqArgs;
    }
    // If we in extended seqId range, we can't return the value because
    // the vanilla code expects the high byte to be seqArgs. So we will
    // instead return some seqId that is unknown to the game.
    return NA_BGM_UNKNOWN;
}

// Internal functions, the exports are located in sequence.c
u8 AudioApi_GetSequenceFlagsInternal(s32 seqId) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return 0;
    }
    return sExtSeqFlags[seqId];
}

void AudioApi_SetSequenceFlagsInternal(s32 seqId, u8 flags) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return;
    }
    sExtSeqFlags[seqId] = flags;
}

// Queues a bgm sequence directly to the internal audio queue system
// Skips the external audio command process
RECOMP_EXPORT void AudioApi_PlayMainBgm(s32 seqId) {
    AUDIOCMD_EXTENDED_GLOBAL_INIT_SEQPLAYER(SEQ_PLAYER_BGM_MAIN, seqId, 1);
}

RECOMP_EXPORT s32 AudioApi_IsSequencePlaying(s32 seqId) {
    u8 seqPlayerIndex = SEQ_PLAYER_BGM_MAIN;
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if (seqFlags & SEQ_FLAG_FANFARE) {
        seqPlayerIndex = SEQ_PLAYER_FANFARE;
    } else if (seqFlags & SEQ_FLAG_FANFARE_KAMARO) {
        seqPlayerIndex = SEQ_PLAYER_FANFARE;
    }

    if (seqId == AudioApi_GetActiveSeqId(seqPlayerIndex)) {
        return true;
    } else {
        return false;
    }
}

RECOMP_PATCH s32 Audio_IsSequencePlaying(u8 seqId) {
    return AudioApi_IsSequencePlaying(seqId);
}


// ======== Z_OBJ_SOUND FUNCTIONS ========

RECOMP_EXPORT void AudioApi_StartObjSoundFanfare(u8 seqPlayerIndex, Vec3f* pos, s32 seqId, u16 seqArgs) {
    s32 curSeqId = AudioApi_GetActiveSeqId(seqPlayerIndex);

    // @mod Is the AudioSeq_IsSeqCmdNotQueued condition a bug? There is no cmdOp, but we're checking the mask

    if ((curSeqId == NA_BGM_FINAL_HOURS) || sIsFinalHoursOrSoaring ||
        !AudioSeq_IsSeqCmdNotQueued((seqPlayerIndex << 0x18) + NA_BGM_FINAL_HOURS, SEQCMD_ALL_MASK)) {
        sIsFinalHoursOrSoaring = true;
    } else if (pos != NULL) {
        if (seqId != curSeqId &&
            !gAudioCtx.seqPlayers[seqPlayerIndex].enabled && (sExtObjSoundMainBgmSeqId == NA_BGM_GENERAL_SFX)) {

            SEQCMD_EXTENDED_PLAY_SEQUENCE(seqPlayerIndex, ((AudioThread_NextRandom() % 30) & 0xFF) + 1,
                                          seqArgs, seqId);

            sExtObjSoundMainBgmSeqId = seqId;
        }

        Audio_SetObjSoundProperties(seqPlayerIndex, pos, 0x7F, 320.0f, 1280.0f, 1.0f, 0.0f);
    } else {
        SEQCMD_STOP_SEQUENCE(seqPlayerIndex, 5);
    }
}

RECOMP_PATCH void Audio_StartObjSoundFanfare(u8 seqPlayerIndex, Vec3f* pos, s8 seqId, u16 seqArgs) {
    AudioApi_StartObjSoundFanfare(seqPlayerIndex, pos, seqId, seqArgs);
}

RECOMP_EXPORT void AudioApi_PlayObjSoundBgm(Vec3f* pos, s32 seqId) {
    u16 sp36;
    s32 sp2C;
    s32 seqId0 = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 fadeInDuration;

    // @mod Is the AudioSeq_IsSeqCmdNotQueued condition a bug? There is no cmdOp, but we're checking the mask

    if ((seqId0 == NA_BGM_FINAL_HOURS) || sIsFinalHoursOrSoaring
        || !AudioSeq_IsSeqCmdNotQueued(NA_BGM_FINAL_HOURS, SEQCMD_ALL_MASK)) {
        sIsFinalHoursOrSoaring = true;
        return;
    }

    if (seqId0 == NA_BGM_SONG_OF_SOARING) {
        sIsFinalHoursOrSoaring = true;
    }

    if (pos != NULL) {
        if (seqId == NA_BGM_ASTRAL_OBSERVATORY) {

            if (seqId != seqId0 && !sAllPlayersMutedExceptSystemAndOcarina) {
                SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, 0, seqId);
                sExtObjSoundMainBgmSeqId = seqId;
            } else if (seqId == seqId0 && (sExtObjSoundMainBgmSeqId == NA_BGM_GENERAL_SFX)) {
                sExtObjSoundMainBgmSeqId = seqId;
            }

            Audio_SetObjSoundProperties(SEQ_PLAYER_BGM_MAIN, pos, 0x20, 100.0f, 1500.0f, 0.9f, 0.0f);
        } else {
            if (sExtObjSoundMainBgmSeqId == NA_BGM_GENERAL_SFX) {
                fadeInDuration = ((AudioThread_NextRandom() % 30) & 0xFF) + 1;
                SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeInDuration, 0x7F00, seqId);
                sExtObjSoundMainBgmSeqId = seqId;
            }

            if (seqId == NA_BGM_MILK_BAR_DUPLICATE) {
                Audio_SetObjSoundProperties(SEQ_PLAYER_BGM_MAIN, pos, 0x1E3, 0.0f, 600.0f, 0.9f, 0.55f);
            } else if (seqId == NA_BGM_MILK_BAR) {
                Audio_SetObjSoundProperties(SEQ_PLAYER_BGM_MAIN, pos, 0x1FF, 0.0f, 600.0f, 0.9f, 0.55f);
            } else {
                Audio_SetObjSoundProperties(SEQ_PLAYER_BGM_MAIN, pos, 0x3F, 0.0f, 600.0f, 0.9f, 0.55f);
            }
        }
    } else {
        if (sExtObjSoundMainBgmSeqId == NA_BGM_ASTRAL_OBSERVATORY) {
            AUDIOCMD_GLOBAL_SET_CHANNEL_MASK(SEQ_PLAYER_BGM_MAIN, 0xFFFF);
            AUDIOCMD_CHANNEL_SET_VOL_SCALE(SEQ_PLAYER_BGM_MAIN, AUDIOCMD_ALL_CHANNELS, 1.0f);
            SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 10, NA_BGM_CAVERN);
        } else {
            SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 5);
        }

        sExtObjSoundMainBgmSeqId = NA_BGM_GENERAL_SFX;
    }
}

RECOMP_PATCH void Audio_PlayObjSoundBgm(Vec3f* pos, s8 seqId) {
    AudioApi_PlayObjSoundBgm(pos, seqId);
}

RECOMP_EXPORT void AudioApi_PlayObjSoundFanfare(Vec3f* pos, s32 seqId) {
    s32 requestFanfare = false;

    if (sExtObjSoundFanfareSeqId == NA_BGM_GENERAL_SFX) {
        // No spatial fanfare is currently playing
        requestFanfare = true;
    } else if (SEQ_SCREEN_WEIGHTED_DIST(pos) < SEQ_SCREEN_WEIGHTED_DIST(&sObjSoundFanfarePos)) {
        // The spatial fanfare requested is closer than the spatial fanfare currently playing
        requestFanfare = true;
    }

    if (requestFanfare) {
        sObjSoundFanfarePos.x = pos->x;
        sObjSoundFanfarePos.y = pos->y;
        sObjSoundFanfarePos.z = pos->z;
        sExtObjSoundFanfareSeqId = seqId;
        sObjSoundFanfareRequested = true;
    }
}

RECOMP_PATCH void Audio_PlayObjSoundFanfare(Vec3f* pos, s8 seqId) {
    AudioApi_PlayObjSoundFanfare(pos, seqId);
}

RECOMP_PATCH void Audio_UpdateObjSoundFanfare(void) {
    if (sObjSoundFanfareRequested && (sAudioPauseState == AUDIO_PAUSE_STATE_CLOSED)) {
        if (sExtObjSoundFanfareSeqId != NA_BGM_GENERAL_SFX) {
            AudioApi_StartObjSoundFanfare(SEQ_PLAYER_FANFARE, &sObjSoundFanfarePos, sExtObjSoundFanfareSeqId, 0);

            if (AudioApi_GetActiveSeqId(SEQ_PLAYER_FANFARE) == NA_BGM_DISABLED) {
                Audio_MuteBgmPlayersForFanfare();
            }

            if ((AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN) != NA_BGM_DISABLED) &&
                (AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE) == NA_BGM_DISABLED)) {
                Audio_PlayAmbience(AMBIENCE_ID_09);
            }

            sAudioCutsceneFlag = true;
        } else {
            AudioApi_StartObjSoundFanfare(SEQ_PLAYER_FANFARE, NULL, sExtObjSoundFanfareSeqId, 0);
            if (AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN) != NA_BGM_DISABLED) {
                SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_AMBIENCE, 0);
            }
            sObjSoundFanfareRequested = false;
            sExtObjSoundMainBgmSeqId = NA_BGM_GENERAL_SFX;
            sAudioCutsceneFlag = false;
        }
        sExtObjSoundFanfareSeqId = NA_BGM_GENERAL_SFX;
    }
}


// ======== OTHER START STOP FUNCTIONS ========

RECOMP_EXPORT void AudioApi_PlaySubBgm(s32 seqId, u16 seqArgs) {
    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_SUB, VOL_SCALE_INDEX_BGM_SUB, 0x7F, 0);
    SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0, seqArgs, seqId);
    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0, 5);

    SEQCMD_SETUP_RESTORE_SEQPLAYER_VOLUME_WITH_SCALE_INDEX(SEQ_PLAYER_BGM_SUB, SEQ_PLAYER_BGM_MAIN, 3, 10);
    SEQCMD_SETUP_SET_CHANNEL_DISABLE_MASK(SEQ_PLAYER_BGM_SUB, SEQ_PLAYER_BGM_MAIN, 0);
}

RECOMP_PATCH void Audio_PlaySubBgm(u16 seqId) {
    AudioApi_PlaySubBgm(seqId & 0xFF, seqId & 0xFF00);
}

RECOMP_EXPORT void AudioApi_StartSubBgmAtPos(u8 seqPlayerIndex, Vec3f* projectedPos, s32 seqId, u8 flags,
                                             f32 minDist, f32 maxDist, f32 arg6) {
    f32 dist = SEQ_SCREEN_WEIGHTED_DIST(projectedPos);
    s32 seqId0 = AudioApi_GetActiveSeqId(seqPlayerIndex);
    u16 seqArgs0 = AudioApi_GetActiveSeqArgs(seqPlayerIndex);

    if (dist > maxDist) {
        if (seqId0 == seqId) {
            Audio_StopSequenceAtPos(seqPlayerIndex, 10);
            sSpatialSeqIsActive[seqPlayerIndex] = false;
        }
        return;
    }

    if ((!gAudioCtx.seqPlayers[seqPlayerIndex].enabled && !sAllPlayersMutedExceptSystemAndOcarina) ||
        (seqId0 == NA_BGM_ENEMY && seqArgs0 == 0x800)) {
        if (seqPlayerIndex == SEQ_PLAYER_BGM_SUB) {
            AudioSeq_SetVolumeScale(seqPlayerIndex, VOL_SCALE_INDEX_BGM_SUB, 0x7F, 1);
        }

        SEQCMD_EXTENDED_PLAY_SEQUENCE(seqPlayerIndex, 1, 0, seqId);

        sSpatialSeqIsActive[seqPlayerIndex] = true;
    }

    Audio_SetSequenceProperties(seqPlayerIndex, projectedPos, flags, minDist, maxDist, 1.0, 0.05f);

    if ((seqPlayerIndex == SEQ_PLAYER_BGM_SUB) && (gAudioCtx.seqPlayers[SEQ_PLAYER_BGM_MAIN].enabled == true)) {
        f32 relVolume = CLAMP(1.0f - ((maxDist - dist) / (maxDist - minDist)), 0.0f, 1.0f);
        u8 targetVolume = relVolume * 127.0f;
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, targetVolume, 10);
        Audio_SplitBgmChannels(0x7F - targetVolume);
    }
}

RECOMP_PATCH void Audio_StartSubBgmAtPos(u8 seqPlayerIndex, Vec3f* projectedPos, u8 seqId, u8 flags,
                                         f32 minDist, f32 maxDist, f32 arg6) {
    AudioApi_StartSubBgmAtPos(seqPlayerIndex, projectedPos, seqId, flags, minDist, maxDist, arg6);
}

// sSpatialSeqNoFilterPos takes priority over sSpatialSeqFilterPos
// Used only by guru guru for song of storms
RECOMP_EXPORT void AudioApi_PlaySubBgmAtPos(Vec3f* pos, s32 seqId, f32 maxDist) {
    if (gAudioSpecId != 0xC) {
        sSpatialSeqNoFilterPos.x = pos->x;
        sSpatialSeqNoFilterPos.y = pos->y;
        sSpatialSeqNoFilterPos.z = pos->z;
        sExtSpatialSeqSeqId = seqId;
        sSpatialSeqMaxDist = maxDist;
        sSpatialSeqFlags |= 2; // Only update volume
        sSpatialSubBgmFadeTimer = 4;
    }
}

RECOMP_PATCH void Audio_PlaySubBgmAtPos(Vec3f* pos, u8 seqId, f32 maxDist) {
    AudioApi_PlaySubBgmAtPos(pos, seqId, maxDist);
}

// Used only by guru guru for song of storms in stock pot from hallway or neighboring room
RECOMP_EXPORT void AudioApi_PlaySubBgmAtPosWithFilter(Vec3f* pos, u8 seqId, f32 maxDist) {
    sSpatialSeqFilterPos.x = pos->x;
    sSpatialSeqFilterPos.y = pos->y;
    sSpatialSeqFilterPos.z = pos->z;
    sExtSpatialSeqSeqId = seqId;
    //! @bug Did not set sSpatialSeqMaxDist = maxDist; This will use the previously set value of sSpatialSeqMaxDist
    sSpatialSeqFlags |= 1; // Update with volume and filter
    sSpatialSubBgmFadeTimer = 4;
}

RECOMP_PATCH void Audio_PlaySubBgmAtPosWithFilter(Vec3f* pos, u8 seqId, f32 maxDist) {
    AudioApi_PlaySubBgmAtPosWithFilter(pos, seqId, maxDist);
}

RECOMP_PATCH void Audio_UpdateSubBgmAtPos(void) {
    if (sSpatialSubBgmFadeTimer != 0) {
        if (sSpatialSeqFlags & 2) {
            // Affects only volume
            AudioApi_StartSubBgmAtPos(SEQ_PLAYER_BGM_SUB, &sSpatialSeqNoFilterPos, sExtSpatialSeqSeqId, 0x20,
                                      100.0f, sSpatialSeqMaxDist, 1.0f);
        } else {
            // Set volume with band-pass filter
            AudioApi_StartSubBgmAtPos(SEQ_PLAYER_BGM_SUB, &sSpatialSeqFilterPos, sExtSpatialSeqSeqId, 0x28,
                                      100.0f, sSpatialSeqMaxDist, 1.0f);
        }

        sSpatialSubBgmFadeTimer--;
        if (sSpatialSubBgmFadeTimer == 0) {
            Audio_StopSequenceAtPos(SEQ_PLAYER_BGM_SUB, 10);
        }

        sSpatialSeqFlags = 0;
    }
}

RECOMP_EXPORT void AudioApi_PlaySequenceAtDefaultPos(u8 seqPlayerIndex, s32 seqId) {
    if (!sAudioCutsceneFlag && (gAudioSpecId != 0xC)) {
        sSpatialSeqFilterPos.x = gSfxDefaultPos.x;
        sSpatialSeqFilterPos.y = gSfxDefaultPos.y;
        sSpatialSeqFilterPos.z = gSfxDefaultPos.z;
        sSpatialSeqMaxDist = 10000.0f;
        sSpatialSeqFadeTimer = 128;
        sExtSpatialSeqSeqId = seqId;
        sSpatialSeqPlayerIndex = seqPlayerIndex;
    }
}

RECOMP_PATCH void Audio_PlaySequenceAtDefaultPos(u8 seqPlayerIndex, u16 seqId) {
    AudioApi_PlaySequenceAtDefaultPos(seqPlayerIndex, seqId & 0xFF);
}

// Used only by minifrog
RECOMP_PATCH void Audio_StopSequenceAtDefaultPos(void) {
    if (gAudioSpecId != 0xC) {
        sSpatialSeqFadeTimer = 1;
        sExtSpatialSeqSeqId = NA_BGM_GENERAL_SFX;
    }
}

// Play the requested sequence at a position. Valid for sequences on players 0 - 3
RECOMP_EXPORT void AudioApi_PlaySequenceAtPos(u8 seqPlayerIndex, Vec3f* pos, s32 seqId, f32 maxDist) {
    s32 curSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);

    if ((!sAudioCutsceneFlag) && (curSeqId != NA_BGM_SONG_OF_SOARING) && (gAudioSpecId != 0xC) && (pos != NULL) &&
        ((sSpatialSeqPlayerIndex != SEQ_PLAYER_BGM_MAIN) || (curSeqId != NA_BGM_FINAL_HOURS))) {
        sSpatialSeqFilterPos.x = pos->x;
        sSpatialSeqFilterPos.y = pos->y;
        sSpatialSeqFilterPos.z = pos->z;
        sSpatialSeqMaxDist = maxDist;
        sSpatialSeqFadeTimer = 2;
        sExtSpatialSeqSeqId = seqId;
        sSpatialSeqPlayerIndex = seqPlayerIndex;
    }
}

RECOMP_PATCH void Audio_PlaySequenceAtPos(u8 seqPlayerIndex, Vec3f* pos, u16 seqId, f32 maxDist) {
    AudioApi_PlaySequenceAtPos(seqPlayerIndex, pos, seqId & 0xFF, maxDist);
}

RECOMP_PATCH void Audio_UpdateSequenceAtPos(void) {
    s32 mainBgmSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 volumeFadeTimer;

    if ((sSpatialSeqFadeTimer != 0) && (sAudioPauseState == AUDIO_PAUSE_STATE_CLOSED)) {
        if ((sExtSpatialSeqSeqId == NA_BGM_GENERAL_SFX) || (mainBgmSeqId == NA_BGM_SONG_OF_SOARING)) {
            volumeFadeTimer = 10;

            if (mainBgmSeqId == NA_BGM_SONG_OF_SOARING) {
                sSpatialSeqFadeTimer = 0;
                volumeFadeTimer = 1;
            } else if (sSpatialSeqFadeTimer < 128) {
                sSpatialSeqFadeTimer--;
            }

            if (sSpatialSeqFadeTimer == 0) {
                Audio_StopSequenceAtPos(sSpatialSeqPlayerIndex, volumeFadeTimer);
                sSpatialSeqIsActive[sSpatialSeqPlayerIndex] = false;
            }
        } else {
            if ((sSpatialSeqPlayerIndex == SEQ_PLAYER_BGM_MAIN) && (mainBgmSeqId == NA_BGM_FINAL_HOURS)) {
                Audio_StopSequenceAtPos(sSpatialSeqPlayerIndex, 10);
                sSpatialSeqIsActive[sSpatialSeqPlayerIndex] = false;
                return;
            }

            AudioApi_StartSubBgmAtPos(sSpatialSeqPlayerIndex, &sSpatialSeqFilterPos, sExtSpatialSeqSeqId, 0x20,
                                      200.0f, sSpatialSeqMaxDist, 1.0f);
            if (!sSpatialSeqIsActive[sSpatialSeqPlayerIndex]) {
                sSpatialSeqFadeTimer = 0;
            }
        }

        if (sSpatialSeqFadeTimer < 128) {
            sExtSpatialSeqSeqId = NA_BGM_GENERAL_SFX;
        }
    }
}


// ======== SCENE + CUTSCENE FUNCTIONS ========

RECOMP_EXPORT void AudioApi_StartSceneSequence(s32 seqId, u16 seqArgs) {
    u8 fadeInDuration = 0;
    u8 skipHarpIntro;
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);
    u8 prevSceneSeqFlags = AudioApi_GetSequenceFlagsInternal(sExtPrevSceneSeqId);

    if ((prevSceneSeqFlags & SEQ_FLAG_RESUME_PREV) && (seqFlags & SEQ_FLAG_RESUME)) {
        // Resume the sequence from the point where it left off last time it was played in the scene
        if ((sSeqResumePoint & 0x3F) != 0) {
            fadeInDuration = 30;
        }

        // Write the sequence resumePoint to resume from into ioPort 7
        AudioApi_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, seqArgs, fadeInDuration, 7, sSeqResumePoint);

        sSeqResumePoint = 0;
    } else {
        // Start the sequence from the beginning

        // Writes to ioPort 7. See `SEQ_FLAG_SKIP_HARP_INTRO` for writing a value of 1 to ioPort 7.
        if (seqFlags & SEQ_FLAG_SKIP_HARP_INTRO) {
            skipHarpIntro = 1;
        } else {
            skipHarpIntro = (u8)SEQ_IO_VAL_NONE;
        }

        AudioApi_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, seqArgs, 0, 7, skipHarpIntro);

        if (!(seqFlags & SEQ_FLAG_RESUME_PREV)) {
            // Reset the sequence resumePoint
            sSeqResumePoint = SEQ_RESUME_POINT_NONE;
        }
    }
    sExtPrevSceneSeqId = seqId;
}

RECOMP_PATCH void Audio_StartSceneSequence(u16 seqId) {
    AudioApi_StartSceneSequence(seqId & 0xFF, seqId & 0xFF00);
}

RECOMP_EXPORT void AudioApi_StartMorningSceneSequence(s32 seqId, u16 seqArgs) {
    if (seqId != NA_BGM_AMBIENCE) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_AMBIENCE, 0);
        AudioApi_StartSceneSequence(seqId, seqArgs);
        AudioApi_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, seqArgs, 0, 0, 1);
    } else {
        Audio_PlayAmbience(AMBIENCE_ID_08);
    }
}

RECOMP_PATCH void Audio_StartMorningSceneSequence(u16 seqId) {
    AudioApi_StartMorningSceneSequence(seqId & 0xFF, seqId & 0xFF00);
}

RECOMP_EXPORT void AudioApi_PlayMorningSceneSequence(s32 seqId, u16 seqArgs, u8 dayMinusOne) {
    AudioApi_StartMorningSceneSequence(seqId, seqArgs);
    SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_BGM_MAIN, 4, dayMinusOne);
}

RECOMP_PATCH void Audio_PlayMorningSceneSequence(u16 seqId, u8 dayMinusOne) {
    AudioApi_PlayMorningSceneSequence(seqId & 0xFF, seqId & 0xFF00, dayMinusOne);
}

RECOMP_EXPORT void AudioApi_PlaySceneSequence(s32 seqId, u16 seqArgs, u8 dayMinusOne) {
    if (sExtRequestedSceneSeqId != seqId || sExtRequestedSceneSeqArgs != seqArgs) {
        if (seqId == NA_BGM_AMBIENCE) {
            Audio_PlayAmbience(AMBIENCE_ID_08);
        } else if ((seqId != NA_BGM_FINAL_HOURS) || (sExtPrevMainBgmSeqId == NA_BGM_DISABLED)) {
            AudioApi_StartSceneSequence(seqId, seqArgs);
            SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_BGM_MAIN, 4, dayMinusOne);
        }
        sExtRequestedSceneSeqId = seqId;
        sExtRequestedSceneSeqArgs = seqArgs;
    }
}

RECOMP_PATCH void Audio_PlaySceneSequence(u16 seqId, u8 dayMinusOne) {
    AudioApi_PlaySceneSequence(seqId & 0xFF, seqId & 0xFF00, dayMinusOne);
}

RECOMP_PATCH void Audio_UpdateSceneSequenceResumePoint(void) {
    s32 seqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if ((seqId != NA_BGM_DISABLED) && (seqFlags & SEQ_FLAG_RESUME)) {
        if (sSeqResumePoint != SEQ_RESUME_POINT_NONE) {
            // Get the current point to resume from
            sSeqResumePoint = gAudioCtx.seqPlayers[SEQ_PLAYER_BGM_MAIN].seqScriptIO[3];
        } else {
            // Initialize the point to resume from to the start of the sequence
            sSeqResumePoint = 0;
        }
    }
}

RECOMP_EXPORT void AudioApi_PlaySequenceInCutscene(s32 seqId, u16 seqArgs) {
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if (seqFlags & SEQ_FLAG_FANFARE) {
        Audio_PlayFanfare(seqId);
    } else if (seqFlags & SEQ_FLAG_FANFARE_KAMARO) {
        SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_FANFARE, 0, seqArgs, seqId);
    } else if (seqFlags & SEQ_FLAG_NO_AMBIENCE) {
        SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0, seqArgs, seqId);
    } else {
        AudioApi_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, seqArgs, 0, 7, SEQ_IO_VAL_NONE);
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_FANFARE, 0x7F, 0);
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0x7F, 0);
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    }
}

RECOMP_PATCH void Audio_PlaySequenceInCutscene(u16 seqId) {
    AudioApi_PlaySequenceInCutscene(seqId & 0xFF, seqId & 0xFF00);
}

RECOMP_EXPORT void AudioApi_StopSequenceInCutscene(s32 seqId) {
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if (seqFlags & SEQ_FLAG_FANFARE) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    } else if (seqFlags & SEQ_FLAG_FANFARE_KAMARO) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    } else if (seqFlags & SEQ_FLAG_NO_AMBIENCE) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0);
    } else {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
    }
}

RECOMP_PATCH void Audio_StopSequenceInCutscene(u16 seqId) {
    AudioApi_StopSequenceInCutscene(seqId & 0xFF);
}


// ======== FANFARE + BACKGROUND MUSIC + AMBIENCE FUNCTIONS ========

RECOMP_EXPORT void AudioApi_PlayBgm_StorePrevBgm(s32 seqId, u16 seqArgs) {
    s32 curSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u16 curSeqArgs = AudioApi_GetActiveSeqArgs(SEQ_PLAYER_BGM_MAIN);
    u8 curSeqFlags = AudioApi_GetSequenceFlagsInternal(curSeqId);

    if (curSeqId == NA_BGM_DISABLED) {
        curSeqId = NA_BGM_GENERAL_SFX;
    }

    if (curSeqId != seqId) {
        Audio_SetSequenceMode(SEQ_MODE_IGNORE);

        // Ensure the sequence about to be stored isn't also storing a separate sequence
        if (!(curSeqFlags & SEQ_FLAG_RESTORE)) {
            sExtPrevMainBgmSeqId = curSeqId;
            sExtPrevMainBgmSeqArgs = curSeqArgs;
        }

        SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, seqArgs + SEQ_FLAG_ASYNC, seqId);
    }
}

RECOMP_PATCH void Audio_PlayBgm_StorePrevBgm(u16 seqId) {
    AudioApi_PlayBgm_StorePrevBgm(seqId & 0xFF, seqId & 0xFF00);
}

RECOMP_PATCH void Audio_RestorePrevBgm(void) {
    s32 curSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 curSeqFlags = AudioApi_GetSequenceFlagsInternal(curSeqId);

    if ((curSeqId != NA_BGM_DISABLED) && (curSeqFlags & SEQ_FLAG_RESTORE)) {
        if ((sExtPrevMainBgmSeqId == NA_BGM_DISABLED) || (sExtPrevMainBgmSeqId == NA_BGM_GENERAL_SFX)) {
            SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
        } else {
            if (sExtPrevMainBgmSeqId == NA_BGM_AMBIENCE) {
                sExtPrevMainBgmSeqId = sExtPrevAmbienceSeqId;
                sExtPrevMainBgmSeqArgs = sExtPrevAmbienceSeqArgs;
            }
            SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, sExtPrevMainBgmSeqArgs + SEQ_FLAG_ASYNC,
                                          sExtPrevMainBgmSeqId);
        }
        sExtPrevMainBgmSeqId = NA_BGM_DISABLED;
        sExtPrevMainBgmSeqArgs = 0x0000;
    }
}

RECOMP_PATCH void Audio_PlayAmbience_StorePrevBgm(u8 ambienceId) {
    s32 seqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u16 seqArgs = AudioApi_GetActiveSeqArgs(SEQ_PLAYER_BGM_MAIN);

    if (seqId != NA_BGM_AMBIENCE) {
        sExtPrevMainBgmSeqId = seqId;
        sExtPrevMainBgmSeqArgs = seqArgs;
    }

    Audio_PlayAmbience(ambienceId);
}


RECOMP_PATCH void Audio_ForceRestorePreviousBgm(void) {
    if (sExtPrevMainBgmSeqId != NA_BGM_DISABLED) {
        SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, sExtPrevMainBgmSeqArgs + SEQ_FLAG_ASYNC,
                                      sExtPrevMainBgmSeqId);
    }
    sExtPrevMainBgmSeqId = NA_BGM_DISABLED;
    sExtPrevMainBgmSeqArgs = 0x0000;
}

RECOMP_PATCH void Audio_StartAmbience(u16 initChannelMask, u16 initMuteChannelMask) {
    u8 channelIndex;

    SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_AMBIENCE, SEQ_PLAYER_IO_PORT_0, 1);
    SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_AMBIENCE, SEQ_PLAYER_IO_PORT_4, (u8)(initChannelMask >> 8));
    SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_AMBIENCE, SEQ_PLAYER_IO_PORT_5, (u8)(initChannelMask & 0xFF));
    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_MAIN, 0x7F, 1);

    if ((AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE) != NA_BGM_DISABLED) &&
        (AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE) != NA_BGM_AMBIENCE)) {
        AudioSeq_StopSequence(SEQ_PLAYER_AMBIENCE, 0);
        AUDIOCMD_GLOBAL_STOP_AUDIOCMDS();
    }

    if ((AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_SUB) == NA_BGM_ENEMY) &&
        (AudioApi_GetActiveSeqArgs(SEQ_PLAYER_BGM_SUB) == 0x800)) {
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0x7F, 1);
    }

    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_AMBIENCE, 0, NA_BGM_AMBIENCE);

    for (channelIndex = 0; channelIndex < SEQ_NUM_CHANNELS; channelIndex++) {
        if (!(initMuteChannelMask & (1 << channelIndex)) && (initChannelMask & (1 << channelIndex))) {
            SEQCMD_SET_CHANNEL_IO(SEQ_PLAYER_AMBIENCE, channelIndex, CHANNEL_IO_PORT_1, 1);
        }
    }
}

RECOMP_PATCH void Audio_PlayAmbience(u8 ambienceId) {
    u8 i = 0;
    u8 channelIndex;
    u8 ioPort;
    u8 ioData;

    s32 curSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE);
    u16 curSeqArgs = AudioApi_GetActiveSeqArgs(SEQ_PLAYER_AMBIENCE);
    u8 curSeqFlags = AudioApi_GetSequenceFlagsInternal(curSeqId);

    if (!((curSeqId != NA_BGM_DISABLED) && (curSeqFlags & SEQ_FLAG_NO_AMBIENCE))) {
        if (curSeqId != NA_BGM_AMBIENCE) {
            sExtPrevAmbienceSeqId = curSeqId;
            sExtPrevAmbienceSeqArgs = curSeqArgs;
        }

        Audio_StartAmbience(sAmbienceData[ambienceId].initChannelMask, sAmbienceData[ambienceId].initMuteChannelMask);

        while ((sAmbienceData[ambienceId].channelProperties[i] != 0xFF) &&
               (i < ARRAY_COUNT(sAmbienceData[ambienceId].channelProperties))) {
            channelIndex = sAmbienceData[ambienceId].channelProperties[i++];
            ioPort = sAmbienceData[ambienceId].channelProperties[i++];
            ioData = sAmbienceData[ambienceId].channelProperties[i++];
            SEQCMD_SET_CHANNEL_IO(SEQ_PLAYER_AMBIENCE, channelIndex, ioPort, ioData);
        }

        SEQCMD_SET_CHANNEL_IO(SEQ_PLAYER_AMBIENCE, AMBIENCE_CHANNEL_SOUND_MODE, CHANNEL_IO_PORT_7, sSoundMode);
    }
}


RECOMP_EXPORT void AudioApi_PlayFanfare(s32 seqId, u16 seqArgs) {
    s32 prevSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_FANFARE);
    u32 outNumFonts;
    u8* prevFontId = AudioThread_GetFontsForSequence(prevSeqId, &outNumFonts);
    u8* fontId = AudioThread_GetFontsForSequence(seqId, &outNumFonts);

    if ((prevSeqId == NA_BGM_DISABLED) || (*prevFontId == *fontId)) {
        sFanfareState = 1;
    } else {
        sFanfareState = 5;
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    }

    sExtFanfareSeqId = seqId;
    sExtFanfareSeqArgs = seqArgs;
}

RECOMP_PATCH void Audio_PlayFanfare(u16 seqId) {
    AudioApi_PlayFanfare(seqId & 0xFF, seqId & 0xFF00);
}


RECOMP_PATCH void Audio_UpdateFanfare(void) {
    if (sFanfareState != 0) {
        if ((sFanfareState != 5) &&
            !AudioSeq_IsSeqCmdNotQueued((SEQCMD_OP_STOP_SEQUENCE << 28) | (SEQ_PLAYER_FANFARE << 24),
                                        SEQCMD_OP_MASK | SEQCMD_ASYNC_ACTIVE | SEQCMD_SEQPLAYER_MASK)) {
            sFanfareState = 0;
        } else {
            sFanfareState--;
            if (sFanfareState == 0) {
                AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(SEQUENCE_TABLE);
                AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(FONT_TABLE);
                if (AudioApi_GetActiveSeqId(SEQ_PLAYER_FANFARE) == NA_BGM_DISABLED) {
                    Audio_MuteBgmPlayersForFanfare();
                }
                SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_FANFARE, 0, sExtFanfareSeqArgs, sExtFanfareSeqId);
                SEQCMD_SET_CHANNEL_DISABLE_MASK(SEQ_PLAYER_BGM_MAIN, 0xFFFF);
            }
        }
    }
}


// ======== MISC FUNCTIONS ========

RECOMP_EXPORT void AudioApi_PlaySequenceWithSeqPlayerIO(s8 seqPlayerIndex, s32 seqId, u16 seqArgs,
                                                        u8 fadeInDuration, s8 ioPort, u8 ioData) {
    SEQCMD_SET_SEQPLAYER_IO(seqPlayerIndex, ioPort, ioData);
    if (seqId >= 2) {
        seqArgs |= SEQ_FLAG_ASYNC;
    }
    SEQCMD_EXTENDED_PLAY_SEQUENCE(seqPlayerIndex, fadeInDuration, seqArgs, seqId);
}

RECOMP_PATCH void Audio_PlaySequenceWithSeqPlayerIO(s8 seqPlayerIndex, u16 seqId,
                                                    u8 fadeInDuration, s8 ioPort, u8 ioData) {
    AudioApi_PlaySequenceWithSeqPlayerIO(seqPlayerIndex, seqId & 0xFF, seqId & 0xFF00,
                                         fadeInDuration, ioPort, ioData);
}


RECOMP_PATCH void Audio_SetSequenceMode(u8 seqMode) {
    s32 volumeFadeInTimer;
    u8 volumeFadeOutTimer;

    s32 seqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    s32 subSeqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_SUB);
    u8 subSeqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if (sExtPrevMainBgmSeqId == NA_BGM_DISABLED) {
        if (sAudioCutsceneFlag || sSpatialSeqIsActive[SEQ_PLAYER_BGM_SUB]) {
            seqMode = SEQ_MODE_IGNORE;
        }

        if (seqId == NA_BGM_DISABLED || seqFlags & SEQ_FLAG_ENEMY || (sPrevSeqMode & 0x7F) == SEQ_MODE_ENEMY) {
            if (seqMode != (sPrevSeqMode & 0x7F)) {
                if (seqMode == SEQ_MODE_ENEMY) {
                    // If only seqMode = SEQ_MODE_ENEMY (Start)
                    volumeFadeInTimer = ABS_ALT(gActiveSeqs[SEQ_PLAYER_BGM_SUB].volScales[1] - sBgmEnemyVolume);

                    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_SUB, VOL_SCALE_INDEX_BGM_SUB,
                                            sBgmEnemyVolume, volumeFadeInTimer);
                    SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 10, 0x800, NA_BGM_ENEMY);

                    if (seqId >= NA_BGM_TERMINA_FIELD) {
                        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB,
                                                0x7F - sBgmEnemyVolume, 10);
                        Audio_SplitBgmChannels(sBgmEnemyVolume);
                    }
                } else if ((sPrevSeqMode & 0x7F) == SEQ_MODE_ENEMY) {
                    // If only sPrevSeqMode = SEQ_MODE_ENEMY (End)
                    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_SUB, 10);

                    if (seqMode == SEQ_MODE_IGNORE) {
                        volumeFadeOutTimer = 0;
                    } else {
                        volumeFadeOutTimer = 10;
                    }

                    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB,
                                            0x7F, volumeFadeOutTimer);
                    Audio_SplitBgmChannels(0);
                }

                sPrevSeqMode = seqMode + 0x80;

            } else if (seqMode == SEQ_MODE_ENEMY) {
                // If both seqMode = sPrevSeqMode = SEQ_MODE_ENEMY
                if (subSeqId == NA_BGM_DISABLED && seqId != NA_BGM_DISABLED && (seqFlags & SEQ_FLAG_ENEMY)) {
                    SEQCMD_EXTENDED_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 10, 0x800, NA_BGM_ENEMY);
                    sPrevSeqMode = seqMode + 0x80;
                }
            }
        } else {
            // Remnant of OoT's Hyrule Field Sequence
            if (seqMode == SEQ_MODE_DEFAULT) {
                if (sPrevSeqMode == SEQ_MODE_STILL) {
                    sNumFramesMoving = 0;
                }
                sNumFramesStill = 0;
                sNumFramesMoving++;
            } else {
                sNumFramesStill++;
            }

            if ((seqMode == SEQ_MODE_STILL) && (sNumFramesStill < 30) && (sNumFramesMoving > 20)) {
                seqMode = SEQ_MODE_DEFAULT;
            }

            sPrevSeqMode = seqMode;
            SEQCMD_SET_SEQPLAYER_IO(SEQ_PLAYER_BGM_MAIN, 2, seqMode);
        }
    }
}

RECOMP_PATCH void Audio_UpdateEnemyBgmVolume(f32 dist) {
    s32 seqId = AudioApi_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    u8 seqFlags = AudioApi_GetSequenceFlagsInternal(seqId);

    if (sPrevSeqMode == (SEQ_MODE_ENEMY | 0x80)) {
        if (dist != sBgmEnemyDist) {
            f32 adjDist = CLAMP(dist - 150.0f, 0.0f, 350.0f);
            sBgmEnemyVolume = ((350.0f - adjDist) * 127.0f) / 350.0f;

            AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_SUB, VOL_SCALE_INDEX_BGM_SUB, sBgmEnemyVolume, 10);

            if ((seqId >= NA_BGM_TERMINA_FIELD) && !(seqFlags & SEQ_FLAG_FANFARE_KAMARO)) {
                AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, (0x7F - sBgmEnemyVolume), 10);
            }
        }

        Audio_SplitBgmChannels(sBgmEnemyVolume);
    }
    sBgmEnemyDist = dist;
}

RECOMP_PATCH void Audio_SetExtraFilter(u8 filter) {
    u8 channelIndex;

    sAudioExtraFilter2 = filter;
    sAudioExtraFilter = filter;
    if (AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE) == NA_BGM_AMBIENCE) {
        for (channelIndex = 0; channelIndex < SEQ_NUM_CHANNELS; channelIndex++) {
            // seq player 4, all channels, slot 6
            AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_AMBIENCE, channelIndex, 6, filter);
        }
    }
}

RECOMP_PATCH void Audio_SetAmbienceChannelIO(u8 channelIndexRange, u8 ioPort, u8 ioData) {
    u8 firstChannelIndex;
    u8 lastChannelIndex;
    u8 channelIndex;

    if ((AudioApi_GetActiveSeqId(SEQ_PLAYER_AMBIENCE) != NA_BGM_AMBIENCE) &&
        AudioSeq_IsSeqCmdNotQueued((SEQCMD_OP_PLAY_SEQUENCE << 28) | NA_BGM_AMBIENCE,
                                   SEQCMD_OP_MASK | SEQCMD_SEQID_MASK)) {
        return;
    }

    // channelIndexRange = 01 on ioPort 1
    if ((((channelIndexRange << 8) + (u32)ioPort) == ((1 << 8) | (u32)1)) &&
        (AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_SUB) != NA_BGM_ROMANI_RANCH)) {
        sRomaniSingingTimer = 0;
    }

    firstChannelIndex = channelIndexRange >> 4;
    lastChannelIndex = channelIndexRange & 0xF;

    if (firstChannelIndex == 0) {
        firstChannelIndex = channelIndexRange & 0xF;
    }

    for (channelIndex = firstChannelIndex; channelIndex <= lastChannelIndex; channelIndex++) {
        SEQCMD_SET_CHANNEL_IO(SEQ_PLAYER_AMBIENCE, channelIndex, ioPort, ioData);
    }
}


// ======== RESET FUNCTIONS ========

RECOMP_PATCH void AudioSeq_ResetActiveSequences(void) {
    u8 seqPlayerIndex;
    u8 scaleIndex;

    for (seqPlayerIndex = 0; seqPlayerIndex < SEQ_PLAYER_MAX; seqPlayerIndex++) {
        sNumSeqRequests[seqPlayerIndex] = 0;

        gExtActiveSeqs[seqPlayerIndex].seqId = NA_BGM_DISABLED;
        gExtActiveSeqs[seqPlayerIndex].seqArgs = 0x0000;
        gExtActiveSeqs[seqPlayerIndex].prevSeqId = NA_BGM_DISABLED;
        gExtActiveSeqs[seqPlayerIndex].prevSeqArgs = 0x0000;
        gActiveSeqs[seqPlayerIndex].tempoTimer = 0;
        gActiveSeqs[seqPlayerIndex].tempoOriginal = 0;
        gActiveSeqs[seqPlayerIndex].tempoCmd = 0;
        gActiveSeqs[seqPlayerIndex].channelPortMask = 0;
        gExtActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
        gActiveSeqs[seqPlayerIndex].setupFadeTimer = 0;
        gActiveSeqs[seqPlayerIndex].freqScaleChannelFlags = 0;
        gActiveSeqs[seqPlayerIndex].volChannelFlags = 0;
        gActiveSeqs[seqPlayerIndex].isWaitingForFonts = false;
        gActiveSeqs[seqPlayerIndex].isSeqPlayerInit = false;

        for (scaleIndex = 0; scaleIndex < VOL_SCALE_INDEX_MAX; scaleIndex++) {
            gActiveSeqs[seqPlayerIndex].volScales[scaleIndex] = 0x7F;
        }

        gActiveSeqs[seqPlayerIndex].volFadeTimer = 1;
        gActiveSeqs[seqPlayerIndex].fadeVolUpdate = true;
    }
}

RECOMP_PATCH void Audio_ResetRequestedSceneSeqId(void) {
    sExtRequestedSceneSeqId = NA_BGM_DISABLED;
    sExtRequestedSceneSeqArgs = 0x0000;
}

RECOMP_HOOK("Audio_ResetData") void AudioApi_ResetData(void) {
    sExtPrevMainBgmSeqId = NA_BGM_DISABLED;
    sExtObjSoundMainBgmSeqId = NA_BGM_GENERAL_SFX;
    sExtPrevAmbienceSeqId = NA_BGM_DISABLED;
    sExtSpatialSeqSeqId = NA_BGM_GENERAL_SFX;
    sExtPrevAmbienceSeqArgs = 0x0000;
    sExtPrevMainBgmSeqArgs = 0x0000;
}

RECOMP_HOOK("Audio_ResetForAudioHeapStep3") void AudioApi_ResetForAudioHeapStep3(void) {
    sExtObjSoundMainBgmSeqId = NA_BGM_GENERAL_SFX;
}

// Update different commands and requests for active sequences.
// This is done in two parts simply so we don't have to patch the very large function
RECOMP_HOOK("AudioSeq_UpdateActiveSequences") void AudioApi_UpdateActiveSequencesPart1() {
    u32 retMsg;
    u8 seqPlayerIndex;

    for (seqPlayerIndex = 0; seqPlayerIndex < SEQ_PLAYER_MAX; seqPlayerIndex++) {

        // The seqPlayer has finished initializing and is currently playing the active sequences
        if (gActiveSeqs[seqPlayerIndex].isSeqPlayerInit && gAudioCtx.seqPlayers[seqPlayerIndex].enabled) {
            gActiveSeqs[seqPlayerIndex].isSeqPlayerInit = false;
        }

        // The seqPlayer is no longer playing the active sequences
        if ((AudioSeq_GetActiveSeqId(seqPlayerIndex) != NA_BGM_DISABLED) &&
            !gAudioCtx.seqPlayers[seqPlayerIndex].enabled && (!gActiveSeqs[seqPlayerIndex].isSeqPlayerInit)) {
            gExtActiveSeqs[seqPlayerIndex].seqId = NA_BGM_DISABLED;
        }

        // Check if the requested sequences is waiting for fonts to load
        if (gActiveSeqs[seqPlayerIndex].isWaitingForFonts) {
            switch ((s32)AudioThread_GetExternalLoadQueueMsg(&retMsg)) {
            case SEQ_PLAYER_BGM_MAIN + 1:
            case SEQ_PLAYER_FANFARE + 1:
            case SEQ_PLAYER_SFX + 1:
            case SEQ_PLAYER_BGM_SUB + 1:
            case SEQ_PLAYER_AMBIENCE + 1:
                // The fonts have been loaded successfully.
                gActiveSeqs[seqPlayerIndex].isWaitingForFonts = false;
                // Queue the same command that was stored previously, but without the 0x8000
                // @mod process using AudioApi_ProcessSeqCmd
                AudioApi_ProcessSeqCmd(&gExtActiveSeqs[seqPlayerIndex].startAsyncSeqCmd);
                break;

            case 0xFF:
                // There was an error in loading the fonts
                gActiveSeqs[seqPlayerIndex].isWaitingForFonts = false;
                break;
            }
        }
    }
}

RECOMP_HOOK_RETURN("AudioSeq_UpdateActiveSequences") void AudioApi_UpdateActiveSequencesPart2() {
    u8 seqPlayerIndex;
    u8 j;

    for (seqPlayerIndex = 0; seqPlayerIndex < SEQ_PLAYER_MAX; seqPlayerIndex++) {
        // Process setup commands
        if (gExtActiveSeqs[seqPlayerIndex].setupCmdNum != 0) {
            // If there is a SeqCmd to reset the audio heap queued, then drop all setup commands
            if (!AudioSeq_IsSeqCmdNotQueued(SEQCMD_OP_RESET_AUDIO_HEAP << 28, SEQCMD_OP_MASK)) {
                gExtActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
                break; // @mod is this a bug? Only the first sequence setup commands are dropped
            }

            // Only process setup commands once the timer reaches zero
            if (gExtActiveSeqs[seqPlayerIndex].setupCmdTimer != 0) {
                gExtActiveSeqs[seqPlayerIndex].setupCmdTimer--;
                continue;
            }

            // Only process setup commands if `seqPlayerIndex` if no longer playing
            // i.e. the `seqPlayer` is no longer enabled
            if (gAudioCtx.seqPlayers[seqPlayerIndex].enabled) {
                continue;
            }

            for (j = 0; j < gExtActiveSeqs[seqPlayerIndex].setupCmdNum; j++) {
                AudioApi_ProcessSeqSetupCmd(&gExtActiveSeqs[seqPlayerIndex].setupCmd[j]);
            }
            gExtActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
        }
    }
}

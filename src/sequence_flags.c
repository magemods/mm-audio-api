#include "modding.h"
#include "global.h"

extern u8 sExtSeqFlags[];
extern u8 sSeqResumePoint;
extern u8 sPrevSceneSeqId;
extern u16 sPrevMainBgmSeqId;
extern u16 sPrevAmbienceSeqId;
extern s8 sAudioCutsceneFlag;
extern u8 sSpatialSeqIsActive[4];
extern u8 sPrevSeqMode;
extern f32 sBgmEnemyDist;
extern s8 sBgmEnemyVolume;
extern u32 sNumFramesStill;
extern u32 sNumFramesMoving;
extern s8 sSoundMode;

extern void Audio_SplitBgmChannels(s8 volumeSplit);
void Audio_StartAmbience(u16 initChannelMask, u16 initMuteChannelMask);

#define AMBIENCE_CHANNEL_PROPERTIES_ENTRIES_MAX 33
typedef struct {
    /* 0x0 */ u16 initChannelMask;     // bitwise flag for 16 channels, channels to initialize
    /* 0x2 */ u16 initMuteChannelMask; // bitwise flag for 16 channels, channels to mute upon initialization
    /* 0x4 */ u8 channelProperties[3 * AMBIENCE_CHANNEL_PROPERTIES_ENTRIES_MAX + 1];
} AmbienceDataIO; // s
extern AmbienceDataIO sAmbienceData[20];

#define SEQ_FLAG_ENEMY (1 << 0)
#define SEQ_FLAG_FANFARE (1 << 1)
#define SEQ_FLAG_FANFARE_KAMARO (1 << 2)
#define SEQ_FLAG_RESTORE (1 << 3)
#define SEQ_FLAG_RESUME (1 << 4)
#define SEQ_FLAG_RESUME_PREV (1 << 5)
#define SEQ_FLAG_SKIP_HARP_INTRO (1 << 6)
#define SEQ_FLAG_NO_AMBIENCE (1 << 7)
#define SEQ_RESUME_POINT_NONE 0xC0

RECOMP_PATCH void Audio_StartSceneSequence(u16 seqId) {
    u8 fadeInDuration = 0;
    u8 skipHarpIntro;

    if ((sExtSeqFlags[sPrevSceneSeqId] & SEQ_FLAG_RESUME_PREV) && (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_RESUME)) {
        // Resume the sequence from the point where it left off last time it was played in the scene
        if ((sSeqResumePoint & 0x3F) != 0) {
            fadeInDuration = 30;
        }

        // Write the sequence resumePoint to resume from into ioPort 7
        Audio_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, fadeInDuration, 7, sSeqResumePoint);

        sSeqResumePoint = 0;
    } else {
        // Start the sequence from the beginning

        // Writes to ioPort 7. See `SEQ_FLAG_SKIP_HARP_INTRO` for writing a value of 1 to ioPort 7.
        if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_SKIP_HARP_INTRO) {
            skipHarpIntro = 1;
        } else {
            skipHarpIntro = (u8)SEQ_IO_VAL_NONE;
        }
        Audio_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, 0, 7, skipHarpIntro);

        if (!(sExtSeqFlags[seqId] & SEQ_FLAG_RESUME_PREV)) {
            // Reset the sequence resumePoint
            sSeqResumePoint = SEQ_RESUME_POINT_NONE;
        }
    }
    sPrevSceneSeqId = seqId & 0xFF;
}

RECOMP_PATCH void Audio_UpdateSceneSequenceResumePoint(void) {
    u16 seqId = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);

    if ((seqId != NA_BGM_DISABLED) && (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_RESUME)) {
        if (sSeqResumePoint != SEQ_RESUME_POINT_NONE) {
            // Get the current point to resume from
            sSeqResumePoint = gAudioCtx.seqPlayers[SEQ_PLAYER_BGM_MAIN].seqScriptIO[3];
        } else {
            // Initialize the point to resume from to the start of the sequence
            sSeqResumePoint = 0;
        }
    }
}

RECOMP_PATCH void Audio_PlaySequenceInCutscene(u16 seqId) {
    if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_FANFARE) {
        Audio_PlayFanfare(seqId);
    } else if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_FANFARE_KAMARO) {
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_FANFARE, 0, seqId);
    } else if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_NO_AMBIENCE) {
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0, seqId);
    } else {
        Audio_PlaySequenceWithSeqPlayerIO(SEQ_PLAYER_BGM_MAIN, seqId, 0, 7, SEQ_IO_VAL_NONE);
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_FANFARE, 0x7F, 0);
        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0x7F, 0);
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    }
}


RECOMP_PATCH void Audio_StopSequenceInCutscene(u16 seqId) {
    if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_FANFARE) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    } else if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_FANFARE_KAMARO) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    } else if (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_NO_AMBIENCE) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0);
    } else {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
    }
}


RECOMP_PATCH s32 Audio_IsSequencePlaying(u8 seqId) {
    u8 seqPlayerIndex = SEQ_PLAYER_BGM_MAIN;

    if (sExtSeqFlags[seqId & 0xFF] & SEQ_FLAG_FANFARE) {
        seqPlayerIndex = SEQ_PLAYER_FANFARE;
    } else if (sExtSeqFlags[seqId & 0xFF] & SEQ_FLAG_FANFARE_KAMARO) {
        seqPlayerIndex = SEQ_PLAYER_FANFARE;
    }

    if (seqId == (AudioSeq_GetActiveSeqId(seqPlayerIndex) & 0xFF)) {
        return true;
    } else {
        return false;
    }
}


RECOMP_PATCH void Audio_PlayBgm_StorePrevBgm(u16 seqId) {
    u16 curSeqId = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);

    if (curSeqId == NA_BGM_DISABLED) {
        curSeqId = NA_BGM_GENERAL_SFX;
    }

    if (curSeqId != seqId) {
        Audio_SetSequenceMode(SEQ_MODE_IGNORE);

        // Ensure the sequence about to be stored isn't also storing a separate sequence
        if (!(sExtSeqFlags[curSeqId] & SEQ_FLAG_RESTORE)) {
            sPrevMainBgmSeqId = curSeqId;
        }

        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, seqId + SEQ_FLAG_ASYNC);
    }
}


RECOMP_PATCH void Audio_RestorePrevBgm(void) {
    if ((AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN) != NA_BGM_DISABLED) &&
        (sExtSeqFlags[AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN) & 0xFF] & SEQ_FLAG_RESTORE)) {
        if ((sPrevMainBgmSeqId == NA_BGM_DISABLED) || (sPrevMainBgmSeqId == NA_BGM_GENERAL_SFX)) {
            SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
        } else {
            if (sPrevMainBgmSeqId == NA_BGM_AMBIENCE) {
                sPrevMainBgmSeqId = sPrevAmbienceSeqId;
            }
            SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, sPrevMainBgmSeqId + SEQ_FLAG_ASYNC);
        }
        sPrevMainBgmSeqId = NA_BGM_DISABLED;
    }
}


RECOMP_PATCH void Audio_SetSequenceMode(u8 seqMode) {
    s32 volumeFadeInTimer;
    u16 seqId;
    u8 volumeFadeOutTimer;

    if ((sPrevMainBgmSeqId == NA_BGM_DISABLED) && (sPrevMainBgmSeqId == NA_BGM_DISABLED)) {
        // clang-format off
        if (sAudioCutsceneFlag || sSpatialSeqIsActive[SEQ_PLAYER_BGM_SUB]) { \
            seqMode = SEQ_MODE_IGNORE;
        }
        // clang-format on

        seqId = gActiveSeqs[SEQ_PLAYER_BGM_MAIN].seqId;

        if ((seqId == NA_BGM_DISABLED) || (sExtSeqFlags[(u8)(seqId & 0xFF)] & SEQ_FLAG_ENEMY) ||
            ((sPrevSeqMode & 0x7F) == SEQ_MODE_ENEMY)) {
            if (seqMode != (sPrevSeqMode & 0x7F)) {
                if (seqMode == SEQ_MODE_ENEMY) {
                    // If only seqMode = SEQ_MODE_ENEMY (Start)
                    volumeFadeInTimer = ABS_ALT(gActiveSeqs[SEQ_PLAYER_BGM_SUB].volScales[1] - sBgmEnemyVolume);

                    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_SUB, VOL_SCALE_INDEX_BGM_SUB, sBgmEnemyVolume,
                                            volumeFadeInTimer);
                    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 10, NA_BGM_ENEMY | 0x800);

                    if (seqId >= NA_BGM_TERMINA_FIELD) {
                        AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0x7F - sBgmEnemyVolume,
                                                10);
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

                    AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, 0x7F, volumeFadeOutTimer);
                    Audio_SplitBgmChannels(0);
                }

                sPrevSeqMode = seqMode + 0x80;
            } else {
                if (seqMode == SEQ_MODE_ENEMY) {
                    // If both seqMode = sPrevSeqMode = SEQ_MODE_ENEMY
                    if ((AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_SUB) == NA_BGM_DISABLED) &&
                        (seqId != NA_BGM_DISABLED) && (sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_ENEMY)) {
                        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_SUB, 10, NA_BGM_ENEMY | 0x800);
                        sPrevSeqMode = seqMode + 0x80;
                    }
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
    f32 adjDist;
    u16 seqId = gActiveSeqs[SEQ_PLAYER_BGM_MAIN].seqId;

    if (sPrevSeqMode == (SEQ_MODE_ENEMY | 0x80)) {
        if (dist != sBgmEnemyDist) {
            // clamp (dist - 150.0f) between 0 and 350
            if (dist < 150.0f) {
                adjDist = 0.0f;
            } else if (dist > 500.0f) {
                adjDist = 350.0f;
            } else {
                adjDist = dist - 150.0f;
            }

            sBgmEnemyVolume = ((350.0f - adjDist) * 127.0f) / 350.0f;
            AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_SUB, VOL_SCALE_INDEX_BGM_SUB, sBgmEnemyVolume, 10);

            if ((seqId >= NA_BGM_TERMINA_FIELD) && !(sExtSeqFlags[seqId & 0xFF & 0xFF] & SEQ_FLAG_FANFARE_KAMARO)) {
                AudioSeq_SetVolumeScale(SEQ_PLAYER_BGM_MAIN, VOL_SCALE_INDEX_BGM_SUB, (0x7F - sBgmEnemyVolume), 10);
            }
        }

        Audio_SplitBgmChannels(sBgmEnemyVolume);
    }
    sBgmEnemyDist = dist;
}

RECOMP_PATCH void Audio_PlayAmbience(u8 ambienceId) {
    u8 i = 0;
    u8 channelIndex;
    u8 ioPort;
    u8 ioData;

    if (!((gActiveSeqs[SEQ_PLAYER_AMBIENCE].seqId != NA_BGM_DISABLED) &&
          (sExtSeqFlags[gActiveSeqs[SEQ_PLAYER_AMBIENCE].seqId & 0xFF & 0xFF] & SEQ_FLAG_NO_AMBIENCE))) {
        if (gActiveSeqs[SEQ_PLAYER_AMBIENCE].seqId != NA_BGM_AMBIENCE) {
            sPrevAmbienceSeqId = gActiveSeqs[SEQ_PLAYER_AMBIENCE].seqId;
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

#ifndef __AUDIO_API_SEQUENCE_H__
#define __AUDIO_API_SEQUENCE_H__

/*! \file audio_api/sequence.h
    \version 0.5.0
    \brief Low level imports for working with sequences
 */

#include "command_macros_base.h"
#include "types.h"

#define SEQ_FLAG_ASYNC 0x8000
#define SEQ_FLAG_ENEMY (1 << 0)
#define SEQ_FLAG_FANFARE (1 << 1)
#define SEQ_FLAG_FANFARE_KAMARO (1 << 2)
#define SEQ_FLAG_RESTORE (1 << 3)
#define SEQ_FLAG_RESUME (1 << 4)
#define SEQ_FLAG_RESUME_PREV (1 << 5)
#define SEQ_FLAG_SKIP_HARP_INTRO (1 << 6)
#define SEQ_FLAG_NO_AMBIENCE (1 << 7)

/**
 * These are the imports that most mods will use, but more imports are available.
 */
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddSequence(AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceSequence(s32 seqId, AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_RestoreSequence(s32 seqId));

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_GetSequenceFont(s32 seqId, s32 fontNum));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddSequenceFont(s32 seqId, s32 fontId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceSequenceFont(s32 seqId, s32 fontNum, s32 fontId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_RestoreSequenceFont(s32 seqId, s32 fontNum));

RECOMP_IMPORT("magemods_audio_api", u8 AudioApi_GetSequenceFlags(s32 seqId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_SetSequenceFlags(s32 seqId, u8 flags));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_RestoreSequenceFlags(s32 seqId));

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_IsSequencePlaying(s32 seqId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlayFanfare(s32 seqId, u16 seqArgs));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlayMainBgm(s32 seqId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlayObjSoundBgm(Vec3f* pos, s32 seqId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlayObjSoundFanfare(Vec3f* pos, s32 seqId));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlaySubBgm(s32 seqId, u16 seqArgs));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_PlaySubBgmAtPos(Vec3f* pos, s32 seqId, f32 maxDist));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_StartSequence(u8 seqPlayerIndex, s32 seqId, u16 seqArgs, u16 fadeInDuration));

RECOMP_IMPORT("magemods_audio_api", void AudioApi_QueueExtendedSeqCmd(u32 op, u32 cmd, u32 arg1, s32 seqId));

/**
 * Extended AudioSeq commands to support more than 256 sequences
 */
#define SEQCMD_ALL_MASK SEQCMD_OP_MASK | SEQCMD_ASYNC_ACTIVE | SEQCMD_SEQPLAYER_MASK | SEQCMD_SEQID_MASK

/**
 * Play a sequence on a given seqPlayer
 *
 * @see SEQCMD_PLAY_SEQUENCE
 */
#define SEQCMD_EXTENDED_PLAY_SEQUENCE(seqPlayerIndex, fadeInDuration, seqArgs, seqId) \
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_PLAY_SEQUENCE,      \
                                 CMD_BBH((u8)seqPlayerIndex, (u8)fadeInDuration, \
                                         (u16)seqArgs | MIN(seqId, NA_BGM_UNKNOWN)), \
                                 0, seqId)

/**
 * Add a sequence to a queue of sequences associated with a given seqPlayer.
 * If the sequence is first in queue, play the sequence
 *
 * @see SEQCMD_QUEUE_SEQUENCE
 */
#define SEQCMD_EXTENDED_QUEUE_SEQUENCE(seqPlayerIndex, fadeInDuration, priority, seqId) \
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_QUEUE_SEQUENCE,     \
                                 CMD_BBBB((u8)seqPlayerIndex, (u8)fadeInDuration, \
                                          (u8)priority, MIN(seqId, NA_BGM_UNKNOWN)), \
                                 0, seqId)

/**
 * Remove a sequence from a queue of sequences associated with a given seqPlayer.
 * If the sequence is first in queue, stop the sequence, and play the next one in queue if any
 *
 * @see SEQCMD_UNQUEUE_SEQUENCE
 */
#define SEQCMD_EXTENDED_UNQUEUE_SEQUENCE(seqPlayerIndex, fadeOutInDuration, seqId) \
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_UNQUEUE_SEQUENCE,   \
                                 CMD_BBBB((u8)seqPlayerIndex, (u8)fadeOutInDuration, \
                                          0, MIN(seqId, NA_BGM_UNKNOWN)), \
                                 0, seqId)

/**
 * Setup a request to play a sequence on a target seqPlayer once a setup seqPlayer is finished playing and disabled.
 * This command is optionally paired with `SEQCMD_SETUP_SET_FADE_IN_TIMER` to set the fade in duration
 *
 * @see SEQCMD_SETUP_PLAY_SEQUENCE
 */
#define SEQCMD_EXTENDED_SETUP_PLAY_SEQUENCE(setupSeqPlayerIndex, targetSeqPlayerIndex, seqArgs, seqId) \
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_SETUP_CMD,          \
                                 CMD_BBH((u8)setupSeqPlayerIndex, (u8)targetSeqPlayerIndex, \
                                         (u16)seqArgs | MIN(seqId, NA_BGM_UNKNOWN)), \
                                 SEQCMD_SUB_OP_SETUP_PLAY_SEQ, seqId)


#endif

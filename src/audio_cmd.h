#ifndef __AUDIO_API_AUDIO_CMD__
#define __AUDIO_API_AUDIO_CMD__

#include "global.h"
#include "modding.h"
#include "command_macros_base.h"
#include "sequence_functions.h"
#include "queue.h"

void AudioApi_ProcessSeqCmd(RecompQueueCmd* cmd);
void AudioApi_ProcessSeqSetupCmd(RecompQueueCmd* setupCmd);
void AudioApi_QueueExtendedSeqCmd(u32 op, u32 cmd, u32 arg1, s32 seqId);


/**
 * Extended AudioThread commands to support more than 256 sequences
 */
typedef enum {
    // Extended Global Commands
    AUDIOCMD_EXTENDED_OP_GLOBAL_SYNC_LOAD_SEQ_PARTS = 0x86,
    AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER = 0x87,
    AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER_SKIP_TICKS = 0x88,
    AUDIOCMD_EXTENDED_OP_GLOBAL_DISCARD_SEQ_FONTS = 0xF7,
    AUDIOCMD_EXTENDED_OP_GLOBAL_ASYNC_LOAD_SEQ = 0xEA,
} AudioThreadCmdExtendedOp;

#define AUDIOCMD_EXTENDED_GLOBAL_SYNC_LOAD_SEQ_PARTS(seqId, flags, data) \
    AudioThread_QueueCmdS32(CMD_BBH(AUDIOCMD_EXTENDED_OP_GLOBAL_SYNC_LOAD_SEQ_PARTS, flags, data), seqId)

#define AUDIOCMD_EXTENDED_GLOBAL_INIT_SEQPLAYER(seqPlayerIndex, seqId, fadeInTimer) \
    AudioThread_QueueCmdS32(CMD_BBH(AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER, \
                                    (u8)seqPlayerIndex, (u16)fadeInTimer), seqId)

#define AUDIOCMD_EXTENDED_GLOBAL_INIT_SEQPLAYER_SKIP_TICKS(seqPlayerIndex, seqId, skipTicks) \
    AudioThread_QueueCmdS32(CMD_BBH(AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER_SKIP_TICKS, \
                                    (u8)seqPlayerIndex, (u16)skipTicks), seqId)

#define AUDIOCMD_EXTENDED_GLOBAL_DISCARD_SEQ_FONTS(seqId)               \
    AudioThread_QueueCmdS32(CMD_BBH(AUDIOCMD_EXTENDED_OP_GLOBAL_DISCARD_SEQ_FONTS, 0, 0), seqId)

#define AUDIOCMD_EXTENDED_GLOBAL_ASYNC_LOAD_SEQ(seqId, retData)         \
    AudioThread_QueueCmdS32(CMD_BBH(AUDIOCMD_EXTENDED_OP_GLOBAL_ASYNC_LOAD_SEQ, 0, retData), seqId)



/**
 * Extended AudioSeq commands to support more than 256 sequences
 */
#define SEQCMD_ALL_MASK SEQCMD_OP_MASK | SEQCMD_ASYNC_ACTIVE | SEQCMD_SEQPLAYER_MASK | SEQCMD_SEQID_MASK
#define SEQ_FLAG_ASYNC 0x8000

typedef enum {
    SEQCMD_EXTENDED_OP_PLAY_SEQUENCE = 0x10,
    SEQCMD_EXTENDED_OP_QUEUE_SEQUENCE = 0x12,
    SEQCMD_EXTENDED_OP_UNQUEUE_SEQUENCE = 0x13,
    SEQCMD_EXTENDED_OP_SETUP_CMD = 0x1C,
} SeqCmdExtendedOp;

/**
 * Play a sequence on a given seqPlayer
 *
 * @see SEQCMD_PLAY_SEQUENCE
 */
#define SEQCMD_EXTENDED_PLAY_SEQUENCE(seqPlayerIndex, fadeInDuration, seqArgs, seqId) \
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_PLAY_SEQUENCE,        \
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
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_QUEUE_SEQUENCE,       \
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
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_UNQUEUE_SEQUENCE,     \
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
    AudioApi_QueueExtendedSeqCmd(SEQCMD_EXTENDED_OP_SETUP_CMD,            \
                               CMD_BBH((u8)setupSeqPlayerIndex, (u8)targetSeqPlayerIndex, \
                                       (u16)seqArgs | MIN(seqId, NA_BGM_UNKNOWN)), \
                               SEQCMD_SUB_OP_SETUP_PLAY_SEQ, seqId)


#endif

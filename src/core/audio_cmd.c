#include <core/audio_cmd.h>
#include <recomp/modding.h>

/**
 * This file patches various functions found in thread.c and sequence.c addding extended audio
 * commands to the existing queue system in order to support more than 255 sequences.
 *
 * For the sequence command system, all of the args are bitpacked into a single u32. This causes
 * problems for commands like SEQCMD_PLAY_SEQUENCE since it must store the the sequence player index,
 * fade in duration, priority, sequence args, and the sequence ID. This system is replaced by a
 * custom queue system from queue.c that allows a lot more data.
 *
 * For the global command system, it already uses a queue similar to the custom version. In fact,
 * queue.c was directly inspired by it. The problem is that it was not efficent in using the
 * available space, usually wasting an entire 32-bit field. The extended commands simply make better
 * usage of the available space.
 */

extern void AudioSeq_ProcessSeqCmd(u32 cmd);
extern void AudioThread_SetFadeInTimer(s32 seqPlayerIndex, s32 fadeTimer);

RecompQueue* sAudioSeqCmdQueue;

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_AudioCmdInit() {
    sAudioSeqCmdQueue = RecompQueue_Create();
}

/**
 * Hook into global audio thread process to support our extended commands
 */
RECOMP_HOOK("AudioThread_ProcessGlobalCmd") void AudioApi_ProcessGlobalCmd(AudioCmd* cmd) {
    switch (cmd->op) {
    case AUDIOCMD_EXTENDED_OP_GLOBAL_SYNC_LOAD_SEQ_PARTS:
        AudioLoad_SyncLoadSeqParts(cmd->asInt, cmd->arg0, cmd->opArgs & 0xFFFF, &gAudioCtx.externalLoadQueue);
        break;

    case AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER:
        AudioLoad_SyncInitSeqPlayer(cmd->arg0, cmd->asInt, 0);
        AudioThread_SetFadeInTimer(cmd->arg0, cmd->opArgs & 0xFFFF);
        break;

    case AUDIOCMD_EXTENDED_OP_GLOBAL_INIT_SEQPLAYER_SKIP_TICKS:
        AudioLoad_SyncInitSeqPlayerSkipTicks(cmd->arg0, cmd->asInt, cmd->opArgs & 0xFFFF);
        AudioThread_SetFadeInTimer(cmd->arg0, 500);
        AudioScript_SkipForwardSequence(&gAudioCtx.seqPlayers[cmd->arg0]);
        break;

    case AUDIOCMD_EXTENDED_OP_GLOBAL_DISCARD_SEQ_FONTS:
        AudioLoad_DiscardSeqFonts(cmd->asInt);
        break;

    case AUDIOCMD_EXTENDED_OP_GLOBAL_ASYNC_LOAD_SEQ:
        AudioLoad_AsyncLoadSeq(cmd->asInt, cmd->arg0, cmd->opArgs & 0xFFFF, &gAudioCtx.externalLoadQueue);
        break;
    }
}

RECOMP_EXPORT void AudioApi_QueueExtendedSeqCmd(u32 op, u32 cmd, u32 arg1, s32 seqId) {
    cmd |= (op & 0xF) << 28;
    RecompQueue_Push(sAudioSeqCmdQueue, op, cmd, arg1, (void**)&seqId);
}

RECOMP_PATCH void AudioSeq_QueueSeqCmd(u32 cmd) {
    u8 op = cmd >> 28;
    RecompQueue_Push(sAudioSeqCmdQueue, op, cmd, 0, 0);
}

RECOMP_PATCH void AudioSeq_ProcessSeqCmds(void) {
    RecompQueue_Drain(sAudioSeqCmdQueue, AudioApi_ProcessSeqCmd);
}

/**
 * Process our extended sequence commands and their non-extended counterparts
 */
void AudioApi_ProcessSeqCmd(RecompQueueCmd* cmd) {
    s32 priority;
    u16 fadeTimer;
    u8 subOp;
    u8 seqPlayerIndex;
    s32 seqId;
    u8 seqArgs;
    u8 found;
    u8 i;
    u32 outNumFonts;

    seqPlayerIndex = (cmd->arg0 & SEQCMD_SEQPLAYER_MASK) >> 24;

    switch (cmd->op) {
    case SEQCMD_OP_PLAY_SEQUENCE:
    case SEQCMD_EXTENDED_OP_PLAY_SEQUENCE:
        // Play a new sequence
        seqId = cmd->op == SEQCMD_EXTENDED_OP_PLAY_SEQUENCE ? cmd->asInt : (cmd->arg0 & SEQCMD_SEQID_MASK);
        seqArgs = (cmd->arg0 & 0xFF00) >> 8;
        // `fadeTimer` is only shifted 13 bits instead of 16 bits.
        // `fadeTimer` continues to be scaled in `AudioSeq_StartSequence`
        fadeTimer = (cmd->arg0 & 0xFF0000) >> 13;
        if (!gActiveSeqs[seqPlayerIndex].isWaitingForFonts && !sStartSeqDisabled) {
            if (seqArgs < 0x80) {
                AudioApi_StartSequence(seqPlayerIndex, seqId, seqArgs, fadeTimer);
            } else {
                // Store the cmd to be called again once the fonts are loaded
                // but changes the command so that next time, the (seqArgs < 0x80) case is taken
                cmd->arg0 = (cmd->arg0 & ~(SEQ_FLAG_ASYNC | SEQCMD_ASYNC_ACTIVE)) + SEQCMD_ASYNC_ACTIVE;
                gExtActiveSeqs[seqPlayerIndex].startAsyncSeqCmd = *cmd;

                gActiveSeqs[seqPlayerIndex].isWaitingForFonts = true;
                gActiveSeqs[seqPlayerIndex].fontId = *AudioThread_GetFontsForSequence(seqId, &outNumFonts);
                AudioSeq_StopSequence(seqPlayerIndex, 1);

                if (gExtActiveSeqs[seqPlayerIndex].prevSeqId != NA_BGM_DISABLED) {
                    if (*AudioThread_GetFontsForSequence(seqId, &outNumFonts) !=
                        *AudioThread_GetFontsForSequence(gExtActiveSeqs[seqPlayerIndex].prevSeqId, &outNumFonts)) {
                        // Discard Seq Fonts
                        AUDIOCMD_EXTENDED_GLOBAL_DISCARD_SEQ_FONTS(seqId);
                    }
                }

                AUDIOCMD_GLOBAL_ASYNC_LOAD_FONT(*AudioThread_GetFontsForSequence(seqId, &outNumFonts),
                                                (u8)((seqPlayerIndex + 1) & 0xFF));
            }
        }
        break;

    case SEQCMD_OP_QUEUE_SEQUENCE:
    case SEQCMD_EXTENDED_OP_QUEUE_SEQUENCE:
        // Queue a sequence into `sExtSeqRequests`
        seqId = cmd->op == SEQCMD_EXTENDED_OP_PLAY_SEQUENCE ? cmd->asInt : (cmd->arg0 & SEQCMD_SEQID_MASK);
        seqArgs = (cmd->arg0 & 0xFF00) >> 8;
        fadeTimer = (cmd->arg0 & 0xFF0000) >> 13;
        priority = seqArgs;

        // Checks if the requested sequence is first in the list of requests
        // If it is already queued and first in the list, then play the sequence immediately
        for (i = 0; i < sNumSeqRequests[seqPlayerIndex]; i++) {
            if (sExtSeqRequests[seqPlayerIndex][i].seqId == seqId) {
                if (i == 0) {
                    AudioApi_StartSequence(seqPlayerIndex, seqId, seqArgs, fadeTimer);
                }
                return;
            }
        }

        // Searches the sequence requests for the first request that does not have a higher priority
        // than the current incoming request
        found = sNumSeqRequests[seqPlayerIndex];
        for (i = 0; i < sNumSeqRequests[seqPlayerIndex]; i++) {
            if (priority >= sExtSeqRequests[seqPlayerIndex][i].priority) {
                found = i;
                break;
            }
        }

        // Check if the queue is full
        if (sNumSeqRequests[seqPlayerIndex] < ARRAY_COUNT(sExtSeqRequests[seqPlayerIndex])) {
            sNumSeqRequests[seqPlayerIndex]++;
        }

        for (i = sNumSeqRequests[seqPlayerIndex] - 1; i != found; i--) {
            // Move all requests of lower priority backwards 1 place in the queue
            // If the queue is full, overwrite the entry with the lowest priority
            sExtSeqRequests[seqPlayerIndex][i].priority = sExtSeqRequests[seqPlayerIndex][i - 1].priority;
            sExtSeqRequests[seqPlayerIndex][i].seqId = sExtSeqRequests[seqPlayerIndex][i - 1].seqId;
        }

        // Fill the newly freed space in the queue with the new request
        sExtSeqRequests[seqPlayerIndex][found].priority = seqArgs;
        sExtSeqRequests[seqPlayerIndex][found].seqId = seqId;

        // The sequence is first in queue, so start playing.
        if (found == 0) {
            AudioApi_StartSequence(seqPlayerIndex, seqId, seqArgs, fadeTimer);
        }
        break;

    case SEQCMD_OP_UNQUEUE_SEQUENCE:
    case SEQCMD_EXTENDED_OP_UNQUEUE_SEQUENCE:
        // Unqueue sequence
        seqId = cmd->op == SEQCMD_EXTENDED_OP_PLAY_SEQUENCE ? cmd->asInt : (cmd->arg0 & SEQCMD_SEQID_MASK);
        fadeTimer = (cmd->arg0 & 0xFF0000) >> 13;

        found = sNumSeqRequests[seqPlayerIndex];
        for (i = 0; i < sNumSeqRequests[seqPlayerIndex]; i++) {
            if (sExtSeqRequests[seqPlayerIndex][i].seqId == seqId) {
                found = i;
                break;
            }
        }

        if (found != sNumSeqRequests[seqPlayerIndex]) {
            // Move all requests of lower priority forward 1 place in the queue
            for (i = found; i < sNumSeqRequests[seqPlayerIndex] - 1; i++) {
                sExtSeqRequests[seqPlayerIndex][i].priority = sExtSeqRequests[seqPlayerIndex][i + 1].priority;
                sExtSeqRequests[seqPlayerIndex][i].seqId = sExtSeqRequests[seqPlayerIndex][i + 1].seqId;
            }
            sNumSeqRequests[seqPlayerIndex]--;
        }

        // If the sequence was first in queue (it is currently playing),
        // Then stop the sequence and play the next sequence in the queue.
        if (found == 0) {
            AudioSeq_StopSequence(seqPlayerIndex, fadeTimer);
            if (sNumSeqRequests[seqPlayerIndex] != 0) {
                AudioApi_StartSequence(seqPlayerIndex, sExtSeqRequests[seqPlayerIndex][0].seqId,
                                       sExtSeqRequests[seqPlayerIndex][0].priority, fadeTimer);
            }
        }
        break;

    case SEQCMD_OP_SETUP_CMD:
    case SEQCMD_EXTENDED_OP_SETUP_CMD:
        // Queue a sub-command to execute once the sequence is finished playing
        subOp = cmd->op == SEQCMD_EXTENDED_OP_SETUP_CMD ? cmd->arg1 : ((cmd->arg0 & 0xF00000) >> 20);
        seqId = cmd->op == SEQCMD_EXTENDED_OP_SETUP_CMD ? cmd->asInt : (cmd->arg0 & SEQCMD_SEQID_MASK);

        if (subOp != SEQCMD_SUB_OP_SETUP_RESET_SETUP_CMDS) {
            // Ensure the maximum number of setup commands is not exceeded
            found = gExtActiveSeqs[seqPlayerIndex].setupCmdNum++;
            if (found < ARRAY_COUNT(gExtActiveSeqs[seqPlayerIndex].setupCmd)) {
                if (subOp == SEQCMD_SUB_OP_SETUP_PLAY_SEQ) {
                    gExtActiveSeqs[seqPlayerIndex].setupCmd[found] =
                        (RecompQueueCmd){ subOp, cmd->arg0, seqPlayerIndex, (void**)&seqId };
                } else {
                    gExtActiveSeqs[seqPlayerIndex].setupCmd[found] =
                        (RecompQueueCmd){ subOp, cmd->arg0, seqPlayerIndex, 0 };
                }
                // Adds a delay of 2 frames before executing any setup commands.
                // This allows setup commands to be requested along with a new sequence on a seqPlayerIndex.
                // This 2 frame delay ensures the player is enabled before its state is checked for
                // the purpose of deciding if the setup commands should be run.
                // Otherwise, the setup commands will be executed before the sequence starts,
                // when the player is still disabled, instead of when the newly played sequence ends.
                gExtActiveSeqs[seqPlayerIndex].setupCmdTimer = 2;
            }
        } else {
            // `SEQCMD_SUB_OP_SETUP_RESET_SETUP_CMDS`
            // Discard all setup command requests on `seqPlayerIndex`
            gExtActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
        }
        break;


    default:
        // Call original function
        AudioSeq_ProcessSeqCmd(cmd->arg0);
        break;
    }
}

/**
 * Process sequence sub-commands
 */
void AudioApi_ProcessSeqSetupCmd(RecompQueueCmd* setupCmd) {
    u8 seqPlayerIndex;
    u8 targetSeqPlayerIndex;
    u8 setupVal2;
    u8 setupVal1;
    s32 seqId;
    u16 channelMask;

    seqPlayerIndex = setupCmd->arg1;
    targetSeqPlayerIndex = (setupCmd->arg0 & 0xF0000) >> 16;
    setupVal2 = (setupCmd->arg0 & 0xFF00) >> 8;
    setupVal1 = setupCmd->arg0 & 0xFF;

    switch (setupCmd->op) {
    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME:
        // Restore `targetSeqPlayerIndex` volume back to normal levels
        AudioSeq_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, setupVal1);
        break;

    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME_IF_QUEUED:
        // Restore `targetSeqPlayerIndex` volume back to normal levels,
        // but only if the number of sequence queue requests from `sSeqRequests`
        // exactly matches the argument to the command
        if (setupVal1 == sNumSeqRequests[seqPlayerIndex]) {
            AudioSeq_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, setupVal2);
        }
        break;

    case SEQCMD_SUB_OP_SETUP_SEQ_UNQUEUE:
        // Unqueue `seqPlayerIndex` from sSeqRequests
        //! @bug this command does not work as intended as unqueueing
        //! the sequence relies on `gActiveSeqs[seqPlayerIndex].seqId`
        //! However, `gActiveSeqs[seqPlayerIndex].seqId` is reset before the sequence on
        //! `seqPlayerIndex` is requested to stop, i.e. before the sequence is disabled and setup
        //! commands (including this command) can run. A simple fix would have been to unqueue based on
        //! `gActiveSeqs[seqPlayerIndex].prevSeqId` instead
        // @mod Use extended cmd, read seqId from gExtActiveSeqs
        SEQCMD_EXTENDED_UNQUEUE_SEQUENCE((u8)(seqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)), 0,
                                         gExtActiveSeqs[seqPlayerIndex].seqId);
        break;

    case SEQCMD_SUB_OP_SETUP_RESTART_SEQ:
        // Restart the currently active sequence on `targetSeqPlayerIndex` with full volume.
        // Sequence on `targetSeqPlayerIndex` must still be active to play (can be muted)
        // @mod Use extended cmd, read seqId from gExtActiveSeqs
        SEQCMD_EXTENDED_PLAY_SEQUENCE((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)),
                                      1, 0, gExtActiveSeqs[targetSeqPlayerIndex].seqId);
        gActiveSeqs[targetSeqPlayerIndex].fadeVolUpdate = true;
        gActiveSeqs[targetSeqPlayerIndex].volScales[1] = 0x7F;
        break;

    case SEQCMD_SUB_OP_SETUP_TEMPO_SCALE:
        // Scale tempo by a multiplicative factor
        SEQCMD_SCALE_TEMPO((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)), setupVal2,
                           setupVal1);
        break;

    case SEQCMD_SUB_OP_SETUP_TEMPO_RESET:
        // Reset tempo to previous tempo
        SEQCMD_RESET_TEMPO((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)), setupVal1);
        break;

    case SEQCMD_SUB_OP_SETUP_PLAY_SEQ:
        // Play the requested sequence
        // Uses the fade timer set by `SEQCMD_SUB_OP_SETUP_SET_FADE_TIMER`
        // @mod Use extended cmd, read seqId from RecompQueueCmd
        seqId = setupCmd->asInt;
        SEQCMD_EXTENDED_PLAY_SEQUENCE((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)),
                                      gActiveSeqs[targetSeqPlayerIndex].setupFadeTimer,
                                      setupVal2 << 8, seqId);
        AudioSeq_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, 0);
        gActiveSeqs[targetSeqPlayerIndex].setupFadeTimer = 0;
        break;

    case SEQCMD_SUB_OP_SETUP_SET_FADE_TIMER:
        // A command specifically to support `SEQCMD_SUB_OP_SETUP_PLAY_SEQ`
        // Sets the fade timer for the sequence requested in `SEQCMD_SUB_OP_SETUP_PLAY_SEQ`
        gActiveSeqs[seqPlayerIndex].setupFadeTimer = setupVal2;
        break;

    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME_WITH_SCALE_INDEX:
        // Restore the volume back to default levels
        // Allows a `scaleIndex` to be specified.
        AudioSeq_SetVolumeScale(targetSeqPlayerIndex, setupVal2, 0x7F, setupVal1);
        break;

    case SEQCMD_SUB_OP_SETUP_POP_PERSISTENT_CACHE:
        // Discard audio data by popping one more audio caches from the audio heap
        if (setupVal1 & (1 << SEQUENCE_TABLE)) {
            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(SEQUENCE_TABLE);
        }
        if (setupVal1 & (1 << FONT_TABLE)) {
            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(FONT_TABLE);
        }
        if (setupVal1 & (1 << SAMPLE_TABLE)) {
            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(SAMPLE_TABLE);
        }
        break;

    case SEQCMD_SUB_OP_SETUP_SET_CHANNEL_DISABLE_MASK:
        // Disable (or reenable) specific channels of `targetSeqPlayerIndex`
        // @mod Build channel mask from setup values
        channelMask = setupCmd->arg0 & 0xFFFF;
        SEQCMD_SET_CHANNEL_DISABLE_MASK((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)),
                                        channelMask);
        break;

    case SEQCMD_SUB_OP_SETUP_SET_SEQPLAYER_FREQ:
        // Scale all channels of `targetSeqPlayerIndex`
        SEQCMD_SET_SEQPLAYER_FREQ((u8)(targetSeqPlayerIndex + (SEQCMD_ASYNC_ACTIVE >> 24)),
                                  setupVal2, setupVal1 * 10);
        break;

    default:
        break;
    }
}

RECOMP_PATCH s32 AudioSeq_IsSeqCmdNotQueued(u32 cmdVal, u32 cmdMask) {
    for (s32 i = 0; i < sAudioSeqCmdQueue->numEntries; i++) {
        RecompQueueCmd* cmd = &sAudioSeqCmdQueue->entries[i];
        if ((cmd->arg0 & cmdMask) == cmdVal) {
            return false;
        }
    }
    return true;
}

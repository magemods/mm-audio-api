#include "global.h"
#include "modding.h"
#include "sequence_functions.h"

#define MML_VERSION_MM  1
#define MML_VERSION     MML_VERSION_MM
#include "audio/aseq.h"

/**
 * The purpose of this file is to patch just a few functions found in seqplayer.c in order to
 * support more than 256 sequences. The vanilla SequencePlayer stuct has an `u8 seqId` on it,
 * so we need to track our own values in the `sExtSeqPlayersSeqId` array below.
 */

#define PROCESS_SCRIPT_END -1

extern u8 sSeqInstructionArgsTable[];

s32 sExtSeqPlayersSeqId[SEQ_PLAYER_MAX] = {0};

void AudioScript_SequencePlayerDisableChannels(SequencePlayer* seqPlayer, u16 channelBitsUnused);
u8 AudioScript_ScriptReadU8(SeqScriptState* state);
s16 AudioScript_ScriptReadS16(SeqScriptState* state);
u16 AudioScript_ScriptReadCompressedU16(SeqScriptState* state);
s32 AudioScript_HandleScriptFlowControl(SequencePlayer* seqPlayer, SeqScriptState* state, s32 cmd, s32 cmdArg);
void AudioScript_SetInstrument(SequenceChannel* channel, u8 instId);
void AudioScript_SequenceChannelSetVolume(SequenceChannel* channel, u8 volume);
void AudioScript_SetChannelPriorities(SequenceChannel* channel, u8 priority);
s32 AudioScript_SeqChannelSetLayer(SequenceChannel* channel, s32 layerIndex);
void AudioScript_SeqLayerFree(SequenceChannel* channel, s32 layerIndex);
void AudioScript_SequenceChannelEnable(SequencePlayer* seqPlayer, u8 channelIndex, void* script);
void AudioScript_SeqLayerProcessScript(SequenceLayer* layer);
u16 AudioScript_GetScriptControlFlowArgument(SeqScriptState* state, u8 cmd);
void AudioScript_SequencePlayerSetupChannels(SequencePlayer* seqPlayer, u16 channelBits);
void* AudioLoad_SyncLoadFont(u32 fontId);
u8* AudioLoad_SyncLoadSeq(s32 seqId);
u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);

RECOMP_EXPORT void AudioApi_SetSeqPlayerSeqId(SequencePlayer* seqPlayer, s32 seqId) {
    sExtSeqPlayersSeqId[seqPlayer->playerIndex] = seqId;
    seqPlayer->seqId = seqId < 0x100 ? seqId : NA_BGM_UNKNOWN;
}
RECOMP_EXPORT s32 AudioApi_GetSeqPlayerSeqId(SequencePlayer* seqPlayer) {
    return sExtSeqPlayersSeqId[seqPlayer->playerIndex];
}

RECOMP_PATCH s32 AudioLoad_SyncInitSeqPlayerInternal(s32 playerIndex, s32 seqId, s32 arg2) {
    SequencePlayer* seqPlayer = &gAudioCtx.seqPlayers[playerIndex];
    u8* seqData;
    s32 index;
    s32 numFonts;
    s32 fontId;

    if (seqId >= gAudioCtx.numSequences) {
        return 0;
    }

    AudioScript_SequencePlayerDisable(seqPlayer);

    if (1) {}
    fontId = 0xFF;
    index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    numFonts = gAudioCtx.sequenceFontTable[index++];

    while (numFonts > 0) {
        fontId = gAudioCtx.sequenceFontTable[index++];
        AudioLoad_SyncLoadFont(fontId);
        numFonts--;
    }

    seqData = AudioLoad_SyncLoadSeq(seqId);
    if (seqData == NULL) {
        return 0;
    }

    AudioScript_ResetSequencePlayer(seqPlayer);

    if (fontId != 0xFF) {
        seqPlayer->defaultFont = AudioLoad_GetRealTableIndex(FONT_TABLE, fontId);
    } else {
        seqPlayer->defaultFont = 0xFF;
    }

    seqPlayer->seqData = seqData;
    seqPlayer->enabled = true;
    seqPlayer->scriptState.pc = seqData;
    seqPlayer->scriptState.depth = 0;
    seqPlayer->delay = 0;
    seqPlayer->finished = false;
    seqPlayer->playerIndex = playerIndex;

    // @mod use our setter from above
    AudioApi_SetSeqPlayerSeqId(seqPlayer, seqId);

    // @mod original function was missing return (but the return value is not used so it's not UB)
    return 0;
}

RECOMP_PATCH void AudioScript_SequencePlayerDisable(SequencePlayer* seqPlayer) {
    AudioScript_SequencePlayerDisableChannels(seqPlayer, 0xFFFF);
    AudioList_ClearNotePool(&seqPlayer->notePool);
    if (!seqPlayer->enabled) {
        return;
    }

    seqPlayer->enabled = false;
    seqPlayer->finished = true;

    s32 seqId = AudioApi_GetSeqPlayerSeqId(seqPlayer);
    if (AudioLoad_IsSeqLoadComplete(seqId)) {
        AudioLoad_SetSeqLoadStatus(seqId, LOAD_STATUS_DISCARDABLE);
    }

    if (AudioLoad_IsFontLoadComplete(seqPlayer->defaultFont)) {
        AudioLoad_SetFontLoadStatus(seqPlayer->defaultFont, LOAD_STATUS_MAYBE_DISCARDABLE);
    }

    if (seqPlayer->defaultFont == gAudioCtx.fontCache.temporary.entries[0].id) {
        gAudioCtx.fontCache.temporary.nextSide = 1;
    } else if (seqPlayer->defaultFont == gAudioCtx.fontCache.temporary.entries[1].id) {
        gAudioCtx.fontCache.temporary.nextSide = 0;
    }
}

RECOMP_PATCH void AudioScript_SequenceChannelProcessScript(SequenceChannel* channel) {
    s32 i;
    u8* data;
    u32 rand;
    s32 seqId;

    SequencePlayer* seqPlayer;

    if (channel->stopScript) {
        goto exit_loop;
    }

    seqPlayer = channel->seqPlayer;
    seqId = AudioApi_GetSeqPlayerSeqId(seqPlayer);
    if (seqPlayer->muted && (channel->muteFlags & MUTE_FLAGS_STOP_SCRIPT)) {
        return;
    }

    if (channel->delay >= 2) {
        channel->delay--;
        goto exit_loop;
    }

    while (true) {
        SeqScriptState* scriptState = &channel->scriptState;
        s32 param;
        s16 temp1;
        u16 cmdArgU16;
        u32 cmdArgs[3];
        s8 cmdArgS8;
        u8 cmd = AudioScript_ScriptReadU8(scriptState);
        u8 lowBits;
        u8 highBits;
        s32 delay;
        s32 temp2;
        u8 phi_v0_3;
        u8 new_var;
        u8 depth;
        u8* seqData = seqPlayer->seqData;
        u32 new_var2;

        // Commands 0xA0 - 0xFF
        if (cmd >= 0xA0) {
            highBits = sSeqInstructionArgsTable[cmd - 0xA0];
            lowBits = highBits & 3;

            // read in arguments for the instruction
            for (i = 0; i < lowBits; i++, highBits <<= 1) {
                if (!(highBits & 0x80)) {
                    cmdArgs[i] = AudioScript_ScriptReadU8(scriptState);
                } else {
                    cmdArgs[i] = AudioScript_ScriptReadS16(scriptState);
                }
            }

            // Control Flow Commands
            if (cmd >= ASEQ_OP_CONTROL_FLOW_FIRST) {
                delay = AudioScript_HandleScriptFlowControl(seqPlayer, scriptState, cmd, cmdArgs[0]);

                if (delay != 0) {
                    if (delay == PROCESS_SCRIPT_END) {
                        AudioScript_SequenceChannelDisable(channel);
                    } else {
                        channel->delay = delay;
                    }
                    break;
                }
                continue;
            }

            switch (cmd) {
                case ASEQ_OP_CHAN_STOP: // channel: stop script
                    channel->stopScript = true;
                    goto exit_loop;

                case ASEQ_OP_CHAN_ALLOCNOTELIST: // channel: reserve notes
                    AudioList_ClearNotePool(&channel->notePool);
                    cmd = (u8)cmdArgs[0];
                    AudioList_FillNotePool(&channel->notePool, cmd);
                    break;

                case ASEQ_OP_CHAN_FREENOTELIST: // channel: unreserve notes
                    AudioList_ClearNotePool(&channel->notePool);
                    break;

                case ASEQ_OP_CHAN_DYNTBL: // channel: set dyntable
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->dynTable = (void*)&seqPlayer->seqData[cmdArgU16];
                    break;

                case ASEQ_OP_CHAN_DYNTBLLOOKUP: // channel: dyn set dyntable
                    if (scriptState->value != -1) {
                        data = (*channel->dynTable)[scriptState->value];
                        cmdArgU16 = (u16)((data[0] << 8) + data[1]);
                        scriptState->pc = (void*)&seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case ASEQ_OP_CHAN_FONTINSTR: // channel: set soundFont and instrument
                    cmd = (u8)cmdArgs[0];

                    if (seqPlayer->defaultFont != 0xFF) {
                        // @mod use seqId obtained from getter
                        cmdArgU16 = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
                        lowBits = gAudioCtx.sequenceFontTable[cmdArgU16];
                        cmd = gAudioCtx.sequenceFontTable[cmdArgU16 + lowBits - cmd];
                    }

                    if (AudioHeap_SearchCaches(FONT_TABLE, CACHE_EITHER, cmd)) {
                        channel->fontId = cmd;
                    }

                    cmdArgs[0] = cmdArgs[1];
                    FALLTHROUGH;
                case ASEQ_OP_CHAN_INSTR: // channel: set instrument
                    cmd = (u8)cmdArgs[0];
                    AudioScript_SetInstrument(channel, cmd);
                    break;

                case ASEQ_OP_CHAN_SHORT: // channel: large notes off
                    channel->largeNotes = false;
                    break;

                case ASEQ_OP_CHAN_NOSHORT: // channel: large notes on
                    channel->largeNotes = true;
                    break;

                case ASEQ_OP_CHAN_VOL: // channel: set volume
                    cmd = (u8)cmdArgs[0];
                    AudioScript_SequenceChannelSetVolume(channel, cmd);
                    channel->changes.s.volume = true;
                    break;

                case ASEQ_OP_CHAN_VOLEXP: // channel: set volume scale
                    cmd = (u8)cmdArgs[0];
                    channel->volumeScale = (f32)(s32)cmd / 128.0f;
                    channel->changes.s.volume = true;
                    break;

                case ASEQ_OP_CHAN_FREQSCALE: // channel: set freqscale
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->freqScale = (f32)(s32)cmdArgU16 / 0x8000;
                    channel->changes.s.freqScale = true;
                    break;

                case ASEQ_OP_CHAN_BEND: // channel: large bend pitch
                    cmd = (u8)cmdArgs[0];
                    cmd += 0x80;
                    channel->freqScale = gBendPitchOneOctaveFrequencies[cmd];
                    channel->changes.s.freqScale = true;
                    break;

                case ASEQ_OP_CHAN_BENDFINE: // channel: small bend pitch
                    cmd = (u8)cmdArgs[0];
                    cmd += 0x80;
                    channel->freqScale = gBendPitchTwoSemitonesFrequencies[cmd];
                    channel->changes.s.freqScale = true;
                    break;

                case ASEQ_OP_CHAN_PAN: // channel: set pan
                    cmd = (u8)cmdArgs[0];
                    channel->newPan = cmd;
                    channel->changes.s.pan = true;
                    break;

                case ASEQ_OP_CHAN_PANWEIGHT: // channel: set pan mix
                    cmd = (u8)cmdArgs[0];
                    channel->panChannelWeight = cmd;
                    channel->changes.s.pan = true;
                    break;

                case ASEQ_OP_CHAN_TRANSPOSE: // channel: transpose
                    cmdArgS8 = (s8)cmdArgs[0];
                    channel->transposition = cmdArgS8;
                    break;

                case ASEQ_OP_CHAN_ENV: // channel: set envelope
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->adsr.envelope = (EnvelopePoint*)&seqPlayer->seqData[cmdArgU16];
                    break;

                case ASEQ_OP_CHAN_RELEASERATE: // channel: set decay index
                    cmd = (u8)cmdArgs[0];
                    channel->adsr.decayIndex = cmd;
                    break;

                case ASEQ_OP_CHAN_VIBDEPTH: // channel: set vibrato depth
                    cmd = (u8)cmdArgs[0];
                    channel->vibrato.vibratoDepthTarget = cmd * 8;
                    channel->vibrato.vibratoDepthStart = 0;
                    channel->vibrato.vibratoDepthChangeDelay = 0;
                    break;

                case ASEQ_OP_CHAN_VIBFREQ: // channel: set vibrato rate
                    cmd = (u8)cmdArgs[0];
                    channel->vibrato.vibratoRateChangeDelay = 0;
                    channel->vibrato.vibratoRateTarget = cmd * 32;
                    channel->vibrato.vibratoRateStart = cmd * 32;
                    break;

                case ASEQ_OP_CHAN_VIBDEPTHGRAD: // channel: set vibrato depth linear
                    cmd = (u8)cmdArgs[0];
                    channel->vibrato.vibratoDepthStart = cmd * 8;
                    cmd = (u8)cmdArgs[1];
                    channel->vibrato.vibratoDepthTarget = cmd * 8;
                    cmd = (u8)cmdArgs[2];
                    channel->vibrato.vibratoDepthChangeDelay = cmd * 16;
                    break;

                case ASEQ_OP_CHAN_VIBFREQGRAD: // channel: set vibratorate linear
                    cmd = (u8)cmdArgs[0];
                    channel->vibrato.vibratoRateStart = cmd * 32;
                    cmd = (u8)cmdArgs[1];
                    channel->vibrato.vibratoRateTarget = cmd * 32;
                    cmd = (u8)cmdArgs[2];
                    channel->vibrato.vibratoRateChangeDelay = cmd * 16;
                    break;

                case ASEQ_OP_CHAN_VIBDELAY: // channel: set vibrato delay
                    cmd = (u8)cmdArgs[0];
                    channel->vibrato.vibratoDelay = cmd * 16;
                    break;

                case ASEQ_OP_CHAN_REVERB: // channel: set reverb volume
                    cmd = (u8)cmdArgs[0];
                    channel->targetReverbVol = cmd;
                    break;

                case ASEQ_OP_CHAN_FONT: // channel: set soundFont
                    cmd = (u8)cmdArgs[0];

                    if (seqPlayer->defaultFont != 0xFF) {
                        // @mod use seqId obtained from getter
                        cmdArgU16 = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
                        lowBits = gAudioCtx.sequenceFontTable[cmdArgU16];
                        cmd = gAudioCtx.sequenceFontTable[cmdArgU16 + lowBits - cmd];
                    }

                    if (AudioHeap_SearchCaches(FONT_TABLE, CACHE_EITHER, cmd)) {
                        channel->fontId = cmd;
                    }
                    break;

                case ASEQ_OP_CHAN_STSEQ: // channel: write into sequence script
                    cmd = (u8)cmdArgs[0];
                    cmdArgU16 = (u16)cmdArgs[1];
                    seqData = &seqPlayer->seqData[cmdArgU16];
                    seqData[0] = (u8)scriptState->value + cmd;
                    break;

                case ASEQ_OP_CHAN_SUB: // channel: subtract -> set value
                case ASEQ_OP_CHAN_LDI: // channel: set value
                case ASEQ_OP_CHAN_AND: // channel: `bit and` -> set value
                    cmdArgS8 = (s8)cmdArgs[0];

                    if (cmd == ASEQ_OP_CHAN_SUB) {
                        scriptState->value -= cmdArgS8;
                    } else if (cmd == ASEQ_OP_CHAN_LDI) {
                        scriptState->value = cmdArgS8;
                    } else {
                        scriptState->value &= cmdArgS8;
                    }
                    break;

                case ASEQ_OP_CHAN_STOPCHAN: // channel: disable channel
                    cmd = (u8)cmdArgs[0];
                    AudioScript_SequenceChannelDisable(seqPlayer->channels[cmd]);
                    break;

                case ASEQ_OP_CHAN_MUTEBHV: // channel: set mute behavior
                    cmd = (u8)cmdArgs[0];
                    channel->muteFlags = cmd;
                    channel->changes.s.volume = true;
                    break;

                case ASEQ_OP_CHAN_LDSEQ: // channel: read sequence -> set value
                    cmdArgU16 = (u16)cmdArgs[0];
                    scriptState->value = *(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value));
                    break;

                case ASEQ_OP_CHAN_LDPTR: // channel:
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->unk_22 = cmdArgU16;
                    break;

                case ASEQ_OP_CHAN_STPTRTOSEQ: // channel: write large into sequence script
                    cmdArgU16 = (u16)cmdArgs[0];
                    seqData = &seqPlayer->seqData[cmdArgU16];
                    seqData[0] = (channel->unk_22 >> 8) & 0xFF;
                    seqData[1] = channel->unk_22 & 0xFF;
                    break;

                case ASEQ_OP_CHAN_EFFECTS: // channel: stereo headset effects
                    cmd = (u8)cmdArgs[0];
                    if (cmd & 0x80) {
                        channel->stereoHeadsetEffects = true;
                    } else {
                        channel->stereoHeadsetEffects = false;
                    }
                    channel->stereoData.asByte = cmd & 0x7F;
                    break;

                case ASEQ_OP_CHAN_NOTEALLOC: // channel: set note allocation policy
                    cmd = (u8)cmdArgs[0];
                    channel->noteAllocPolicy = cmd;
                    break;

                case ASEQ_OP_CHAN_SUSTAIN: // channel: set sustain
                    cmd = (u8)cmdArgs[0];
                    channel->adsr.sustain = cmd;
                    break;

                case ASEQ_OP_CHAN_REVERBIDX: // channel: set reverb index
                    cmd = (u8)cmdArgs[0];
                    channel->reverbIndex = cmd;
                    break;

                case ASEQ_OP_CHAN_DYNCALL: // channel: dyncall
                    if (scriptState->value != -1) {
                        data = (*channel->dynTable)[scriptState->value];
                        depth = scriptState->depth;
                        //! @bug: Missing a stack depth check here
                        scriptState->stack[depth] = scriptState->pc;
                        scriptState->depth++;
                        cmdArgU16 = (u16)((data[0] << 8) + data[1]);
                        scriptState->pc = seqPlayer->seqData + cmdArgU16;
                    }
                    break;

                case ASEQ_OP_CHAN_SAMPLEBOOK: // channel: set book offset
                    cmd = (u8)cmdArgs[0];
                    channel->bookOffset = cmd;
                    break;

                case ASEQ_OP_CHAN_LDPARAMS: // channel:
                    cmdArgU16 = (u16)cmdArgs[0];
                    data = &seqPlayer->seqData[cmdArgU16];
                    channel->muteFlags = *data++;
                    channel->noteAllocPolicy = *data++;
                    AudioScript_SetChannelPriorities(channel, *data++);
                    channel->transposition = (s8)*data++;
                    channel->newPan = *data++;
                    channel->panChannelWeight = *data++;
                    channel->targetReverbVol = *data++;
                    channel->reverbIndex = *data++;
                    //! @bug: Not marking reverb state as changed
                    channel->changes.s.pan = true;
                    break;

                case ASEQ_OP_CHAN_PARAMS: // channel:
                    channel->muteFlags = cmdArgs[0];
                    channel->noteAllocPolicy = cmdArgs[1];
                    cmd = (u8)cmdArgs[2];
                    AudioScript_SetChannelPriorities(channel, cmd);
                    channel->transposition = (s8)AudioScript_ScriptReadU8(scriptState);
                    channel->newPan = AudioScript_ScriptReadU8(scriptState);
                    channel->panChannelWeight = AudioScript_ScriptReadU8(scriptState);
                    channel->targetReverbVol = AudioScript_ScriptReadU8(scriptState);
                    channel->reverbIndex = AudioScript_ScriptReadU8(scriptState);
                    //! @bug: Not marking reverb state as changed
                    channel->changes.s.pan = true;
                    break;

                case ASEQ_OP_CHAN_VIBRESET: // channel: reset vibrato
                    channel->vibrato.vibratoDepthTarget = 0;
                    channel->vibrato.vibratoDepthStart = 0;
                    channel->vibrato.vibratoDepthChangeDelay = 0;
                    channel->vibrato.vibratoRateTarget = 0;
                    channel->vibrato.vibratoRateStart = 0;
                    channel->vibrato.vibratoRateChangeDelay = 0;
                    channel->filter = NULL;
                    channel->gain = 0;
                    channel->adsr.sustain = 0;
                    channel->velocityRandomVariance = 0;
                    channel->gateTimeRandomVariance = 0;
                    channel->combFilterSize = 0;
                    channel->combFilterGain = 0;
                    channel->bookOffset = 0;
                    channel->startSamplePos = 0;
                    channel->unk_E0 = 0;
                    channel->freqScale = 1.0f;
                    break;

                case ASEQ_OP_CHAN_NOTEPRI: // channel: set note priority
                    AudioScript_SetChannelPriorities(channel, (u8)cmdArgs[0]);
                    break;

                case ASEQ_OP_CHAN_GAIN: // channel: set hilo gain
                    cmd = (u8)cmdArgs[0];
                    channel->gain = cmd;
                    break;

                case ASEQ_OP_CHAN_LDFILTER: // channel: set filter
                    cmdArgU16 = (u16)cmdArgs[0];
                    data = seqPlayer->seqData + cmdArgU16;
                    channel->filter = (s16*)data;
                    break;

                case ASEQ_OP_CHAN_FREEFILTER: // channel: clear filter
                    channel->filter = NULL;
                    break;

                case ASEQ_OP_CHAN_FILTER: // channel: load filter
                    cmd = cmdArgs[0];

                    if (channel->filter != NULL) {
                        lowBits = (cmd >> 4) & 0xF; // LowPassCutoff
                        cmd &= 0xF;                 // HighPassCutoff
                        AudioHeap_LoadFilter(channel->filter, lowBits, cmd);
                    }
                    break;

                case ASEQ_OP_CHAN_LDSEQTOPTR: // channel: dynread sequence large
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->unk_22 = *(u16*)(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value * 2));
                    break;

                case ASEQ_OP_CHAN_PTRTODYNTBL: // channel: set dyntable large
                    channel->dynTable = (void*)&seqPlayer->seqData[channel->unk_22];
                    break;

                case ASEQ_OP_CHAN_DYNTBLTOPTR: // channel: read dyntable large
                    channel->unk_22 = ((u16*)(channel->dynTable))[scriptState->value];
                    break;

                case ASEQ_OP_CHAN_DYNTBLV: // channel: read dyntable
                    scriptState->value = (*channel->dynTable)[0][scriptState->value];
                    break;

                case ASEQ_OP_CHAN_RANDTOPTR: // channel: random large
                    channel->unk_22 =
                        (cmdArgs[0] == 0) ? (gAudioCtx.audioRandom & 0xFFFF) : (gAudioCtx.audioRandom % cmdArgs[0]);
                    break;

                case ASEQ_OP_CHAN_RAND: // channel: random value
                    scriptState->value =
                        (cmdArgs[0] == 0) ? (gAudioCtx.audioRandom & 0xFFFF) : (gAudioCtx.audioRandom % cmdArgs[0]);
                    break;

                case ASEQ_OP_CHAN_RANDPTR: // channel: random range large (only cmd that differs from OoT)
                    rand = AudioThread_NextRandom();
                    channel->unk_22 = (cmdArgs[0] == 0) ? (rand & 0xFFFF) : (rand % cmdArgs[0]);
                    channel->unk_22 += cmdArgs[1];
                    temp2 = (channel->unk_22 / 0x100) + 0x80;
                    param = channel->unk_22 % 0x100;
                    channel->unk_22 = (temp2 << 8) | param;
                    break;

                case ASEQ_OP_CHAN_RANDVEL: // channel: set velocity random variance
                    channel->velocityRandomVariance = cmdArgs[0];
                    break;

                case ASEQ_OP_CHAN_RANDGATE: // channel: set gatetime random variance
                    channel->gateTimeRandomVariance = cmdArgs[0];
                    break;

                case ASEQ_OP_CHAN_COMBFILTER: // channel:
                    channel->combFilterSize = cmdArgs[0];
                    channel->combFilterGain = cmdArgs[1];
                    break;

                case ASEQ_OP_CHAN_PTRADD: // channel: add large
                    channel->unk_22 += cmdArgs[0];
                    break;

                case ASEQ_OP_CHAN_SAMPLESTART: // channel:
                    channel->startSamplePos = cmdArgs[0];
                    break;

                case ASEQ_OP_CHAN_UNK_BE: // channel:
                    if (cmdArgs[0] < 5) {
                        if (1) {}
                        if (gAudioCtx.customSeqFunctions[cmdArgs[0]] != NULL) {
                            gAudioCustomSeqFunction = gAudioCtx.customSeqFunctions[cmdArgs[0]];
                            scriptState->value = gAudioCustomSeqFunction(scriptState->value, channel);
                        }
                    }
                    break;

                case ASEQ_OP_CHAN_A0: // channel: read from SfxChannelState using arg
                case ASEQ_OP_CHAN_A1: // channel: read from SfxChannelState using unk_22
                case ASEQ_OP_CHAN_A2: // channel: write to SfxChannelState using arg
                case ASEQ_OP_CHAN_A3: // channel: write to SfxChannelState using unk_22
                    if ((cmd == ASEQ_OP_CHAN_A0) || (cmd == ASEQ_OP_CHAN_A2)) {
                        cmdArgU16 = (u16)cmdArgs[0];
                    } else {
                        cmdArgU16 = channel->unk_22;
                    }

                    if (channel->sfxState != NULL) {
                        if ((cmd == ASEQ_OP_CHAN_A0) || (cmd == ASEQ_OP_CHAN_A1)) {
                            scriptState->value = channel->sfxState[cmdArgU16];
                        } else {
                            channel->sfxState[cmdArgU16] = scriptState->value;
                        }
                    }
                    break;

                case ASEQ_OP_CHAN_A4: // channel:
                    channel->surroundEffectIndex = cmdArgs[0];
                    break;

                case ASEQ_OP_CHAN_A5: // channel:
                    scriptState->value += channel->channelIndex;
                    break;

                case ASEQ_OP_CHAN_A6: // channel:
                    cmd = (u8)cmdArgs[0];
                    cmdArgU16 = (u16)cmdArgs[1];
                    seqData = seqPlayer->seqData + (u32)(cmdArgU16 + channel->channelIndex);
                    seqData[0] = (u8)scriptState->value + cmd;
                    break;

                case ASEQ_OP_CHAN_A7: // channel:
                    new_var2 = (cmdArgs[0] & 0x80);
                    new_var = (scriptState->value & 0x80);

                    if (!new_var2) {
                        phi_v0_3 = scriptState->value << (cmdArgs[0] & 0xF);
                    } else {
                        phi_v0_3 = scriptState->value >> (cmdArgs[0] & 0xF);
                    }

                    if (cmdArgs[0] & 0x40) {
                        phi_v0_3 &= (u8)~0x80;
                        phi_v0_3 |= new_var;
                    }

                    scriptState->value = phi_v0_3;
                    break;
            }
            continue;
        }

        // Commands 0x70 - 0x9F
        if (cmd >= 0x70) {
            lowBits = cmd & 0x7;

            if (((cmd & 0xF8) != ASEQ_OP_CHAN_STIO) && (lowBits >= 4)) {
                lowBits = 0;
            }

            switch (cmd & 0xF8) {
                case ASEQ_OP_CHAN_TESTLAYER: // channel: test layer is finished
                    if (channel->layers[lowBits] != NULL) {
                        scriptState->value = channel->layers[lowBits]->finished;
                    } else {
                        scriptState->value = -1;
                    }
                    break;

                case ASEQ_OP_CHAN_LDLAYER: // channel: set layer
                    cmdArgU16 = AudioScript_ScriptReadS16(scriptState);
                    if (!AudioScript_SeqChannelSetLayer(channel, lowBits)) {
                        channel->layers[lowBits]->scriptState.pc = &seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case ASEQ_OP_CHAN_DELLAYER: // channel: free layer
                    AudioScript_SeqLayerFree(channel, lowBits);
                    break;

                case ASEQ_OP_CHAN_DYNLDLAYER: // channel: dynset layer
                    if ((scriptState->value != -1) && (AudioScript_SeqChannelSetLayer(channel, lowBits) != -1)) {
                        data = (*channel->dynTable)[scriptState->value];
                        cmdArgU16 = (data[0] << 8) + data[1];
                        channel->layers[lowBits]->scriptState.pc = &seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case ASEQ_OP_CHAN_STIO: // channel: io write value
                    channel->seqScriptIO[lowBits] = scriptState->value;
                    break;

                case ASEQ_OP_CHAN_RLDLAYER: // channel: set layer relative
                    temp1 = AudioScript_ScriptReadS16(scriptState);
                    if (!AudioScript_SeqChannelSetLayer(channel, lowBits)) {
                        channel->layers[lowBits]->scriptState.pc = &scriptState->pc[temp1];
                    }
                    break;
            }
            continue;
        }

        // Commands 0x00 - 0x6F
        lowBits = cmd & 0xF;

        switch (cmd & 0xF0) {
            case ASEQ_OP_CHAN_CDELAY: // channel: delay short
                channel->delay = lowBits;
                if (lowBits == 0) {
                    break;
                }
                goto exit_loop;

            case ASEQ_OP_CHAN_LDSAMPLE: // channel: load sample
                if (lowBits < 8) {
                    channel->seqScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                    if (AudioLoad_SlowLoadSample(channel->fontId, scriptState->value,
                                                 &channel->seqScriptIO[lowBits]) == -1) {}
                } else {
                    lowBits -= 8;
                    channel->seqScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                    if (AudioLoad_SlowLoadSample(channel->fontId, channel->unk_22 + 0x100,
                                                 &channel->seqScriptIO[lowBits]) == -1) {}
                }
                break;

            case ASEQ_OP_CHAN_LDIO: // channel: io read value
                scriptState->value = channel->seqScriptIO[lowBits];
                if (lowBits < 2) {
                    channel->seqScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                }
                break;

            case ASEQ_OP_CHAN_SUBIO: // channel: io read value subtract
                scriptState->value -= channel->seqScriptIO[lowBits];
                break;

            case ASEQ_OP_CHAN_LDCHAN: // channel: start channel
                cmdArgU16 = AudioScript_ScriptReadS16(scriptState);
                AudioScript_SequenceChannelEnable(seqPlayer, lowBits, &seqPlayer->seqData[cmdArgU16]);
                break;

            case ASEQ_OP_CHAN_STCIO: // channel: io write value 2
                cmd = AudioScript_ScriptReadU8(scriptState);
                seqPlayer->channels[lowBits]->seqScriptIO[cmd] = scriptState->value;
                break;

            case ASEQ_OP_CHAN_LDCIO: // channel: io read value 2
                cmd = AudioScript_ScriptReadU8(scriptState);
                scriptState->value = seqPlayer->channels[lowBits]->seqScriptIO[cmd];
                break;
        }
    }
exit_loop:

    for (i = 0; i < ARRAY_COUNT(channel->layers); i++) {
        if (channel->layers[i] != NULL) {
            AudioScript_SeqLayerProcessScript(channel->layers[i]);
        }
    }
}

RECOMP_PATCH void AudioScript_SequencePlayerProcessSequence(SequencePlayer* seqPlayer) {
    u8 cmd;
    u8 cmdLowBits;
    SeqScriptState* seqScript = &seqPlayer->scriptState;
    s16 tempS;
    u16 temp;
    s32 i;
    s32 value;
    u8* data1;
    u8* data2;
    u8* data3;
    u8* data4;
    s32 tempoChange;
    s32 j;
    SequenceChannel* channel;
    u16* new_var;
    s32 delay;
    s32 seqId = AudioApi_GetSeqPlayerSeqId(seqPlayer);

    if (!seqPlayer->enabled) {
        return;
    }

    // @mod use seqId obtained from getter
    if (!AudioLoad_IsSeqLoadComplete(seqId) || !AudioLoad_IsFontLoadComplete(seqPlayer->defaultFont)) {
        // These function calls serve no purpose
        if (AudioLoad_IsSeqLoadComplete(seqId)) {}
        if (AudioLoad_IsSeqLoadComplete(seqPlayer->defaultFont)) {}

        AudioScript_SequencePlayerDisable(seqPlayer);
        return;
    }

    // @mod use seqId obtained from getter
    AudioLoad_SetSeqLoadStatus(seqId, LOAD_STATUS_COMPLETE);
    AudioLoad_SetFontLoadStatus(seqPlayer->defaultFont, LOAD_STATUS_COMPLETE);

    if (seqPlayer->muted && (seqPlayer->muteFlags & MUTE_FLAGS_STOP_SCRIPT)) {
        return;
    }

    seqPlayer->scriptCounter++;

    tempoChange = seqPlayer->tempo + seqPlayer->tempoChange;
    if (tempoChange > gAudioCtx.maxTempo) {
        tempoChange = gAudioCtx.maxTempo;
    }

    seqPlayer->tempoAcc += tempoChange;

    if (seqPlayer->tempoAcc < gAudioCtx.maxTempo) {
        return;
    }

    seqPlayer->tempoAcc -= (u16)gAudioCtx.maxTempo;
    seqPlayer->unk_16++;

    if (seqPlayer->stopScript == true) {
        return;
    }

    if (seqPlayer->delay > 1) {
        seqPlayer->delay--;
    } else {
        seqPlayer->recalculateVolume = true;

        while (true) {
            cmd = AudioScript_ScriptReadU8(seqScript);

            // 0xF2 and above are "flow control" commands, including termination.
            if (cmd >= ASEQ_OP_CONTROL_FLOW_FIRST) {
                delay = AudioScript_HandleScriptFlowControl(
                    seqPlayer, seqScript, cmd, AudioScript_GetScriptControlFlowArgument(&seqPlayer->scriptState, cmd));

                if (delay != 0) {
                    if (delay == -1) {
                        AudioScript_SequencePlayerDisable(seqPlayer);
                    } else {
                        seqPlayer->delay = delay;
                    }
                    break;
                }
                continue;
            }

            // Commands 0xC0 - 0xF1
            if (cmd >= 0xC0) {
                switch (cmd) {
                    case ASEQ_OP_SEQ_ALLOCNOTELIST: // seqPlayer: reserve notes
                        AudioList_ClearNotePool(&seqPlayer->notePool);
                        cmd = AudioScript_ScriptReadU8(seqScript);
                        AudioList_FillNotePool(&seqPlayer->notePool, cmd);
                        break;

                    case ASEQ_OP_SEQ_FREENOTELIST: // seqPlayer: unreserve notes
                        AudioList_ClearNotePool(&seqPlayer->notePool);
                        break;

                    case ASEQ_OP_SEQ_TRANSPOSE: // seqPlayer: transpose
                        seqPlayer->transposition = 0;
                        FALLTHROUGH;
                    case ASEQ_OP_SEQ_RTRANSPOSE: // seqPlayer: transpose relative
                        seqPlayer->transposition += (s8)AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_TEMPO: // seqPlayer: set tempo
                        seqPlayer->tempo = AudioScript_ScriptReadU8(seqScript) * TATUMS_PER_BEAT;
                        if (seqPlayer->tempo > gAudioCtx.maxTempo) {
                            seqPlayer->tempo = gAudioCtx.maxTempo;
                        }

                        if ((s16)seqPlayer->tempo <= 0) {
                            seqPlayer->tempo = 1;
                        }
                        break;

                    case ASEQ_OP_SEQ_TEMPOCHG: // seqPlayer: add tempo
                        seqPlayer->tempoChange = (s8)AudioScript_ScriptReadU8(seqScript) * TATUMS_PER_BEAT;
                        break;

                    case ASEQ_OP_SEQ_VOLMODE: // seqPlayer: change volume
                        cmd = AudioScript_ScriptReadU8(seqScript);
                        temp = AudioScript_ScriptReadS16(seqScript);
                        switch (cmd) {
                            case SEQPLAYER_STATE_0:
                            case SEQPLAYER_STATE_FADE_IN:
                                if (seqPlayer->state != SEQPLAYER_STATE_FADE_OUT) {
                                    seqPlayer->storedFadeTimer = temp;
                                    seqPlayer->state = cmd;
                                }
                                break;

                            case SEQPLAYER_STATE_FADE_OUT:
                                seqPlayer->fadeTimer = temp;
                                seqPlayer->state = cmd;
                                seqPlayer->fadeVelocity = (0.0f - seqPlayer->fadeVolume) / (s32)seqPlayer->fadeTimer;
                                break;
                        }
                        break;

                    case ASEQ_OP_SEQ_VOL: // seqPlayer: set volume
                        value = AudioScript_ScriptReadU8(seqScript);
                        switch (seqPlayer->state) {
                            case SEQPLAYER_STATE_FADE_IN:
                                seqPlayer->state = SEQPLAYER_STATE_0;
                                seqPlayer->fadeVolume = 0.0f;
                                FALLTHROUGH;
                            case SEQPLAYER_STATE_0:
                                seqPlayer->fadeTimer = seqPlayer->storedFadeTimer;
                                if (seqPlayer->storedFadeTimer != 0) {
                                    seqPlayer->fadeVelocity =
                                        ((value / 127.0f) - seqPlayer->fadeVolume) / (s32)seqPlayer->fadeTimer;
                                } else {
                                    seqPlayer->fadeVolume = value / 127.0f;
                                }
                                break;

                            case SEQPLAYER_STATE_FADE_OUT:
                                break;
                        }
                        break;

                    case ASEQ_OP_SEQ_VOLSCALE: // seqPlayer: set volume scale
                        seqPlayer->fadeVolumeScale = (s8)AudioScript_ScriptReadU8(seqScript) / 127.0f;
                        break;

                    case ASEQ_OP_SEQ_INITCHAN: // seqPlayer: initialize channels
                        temp = AudioScript_ScriptReadS16(seqScript);
                        AudioScript_SequencePlayerSetupChannels(seqPlayer, temp);
                        break;

                    case ASEQ_OP_SEQ_FREECHAN: // seqPlayer: disable channels
                        AudioScript_ScriptReadS16(seqScript);
                        break;

                    case ASEQ_OP_SEQ_MUTESCALE: // seqPlayer: set mute scale
                        seqPlayer->muteVolumeScale = (s8)AudioScript_ScriptReadU8(seqScript) / 127.0f;
                        break;

                    case ASEQ_OP_SEQ_MUTE: // seqPlayer: mute
                        seqPlayer->muted = true;
                        break;

                    case ASEQ_OP_SEQ_MUTEBHV: // seqPlayer: set mute behavior
                        seqPlayer->muteFlags = AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_LDSHORTGATEARR: // seqPlayer: set short note gatetime table
                    case ASEQ_OP_SEQ_LDSHORTVELARR:  // seqPlayer: set short note velocity table
                        temp = AudioScript_ScriptReadS16(seqScript);
                        data3 = &seqPlayer->seqData[temp];
                        if (cmd == ASEQ_OP_SEQ_LDSHORTVELARR) {
                            seqPlayer->shortNoteVelocityTable = data3;
                        } else {
                            seqPlayer->shortNoteGateTimeTable = data3;
                        }
                        break;

                    case ASEQ_OP_SEQ_NOTEALLOC: // seqPlayer: set note allocation policy
                        seqPlayer->noteAllocPolicy = AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_RAND: // seqPlayer: random value
                        cmd = AudioScript_ScriptReadU8(seqScript);
                        if (cmd == 0) {
                            seqScript->value = (gAudioCtx.audioRandom >> 2) & 0xFF;
                        } else {
                            seqScript->value = (gAudioCtx.audioRandom >> 2) % cmd;
                        }
                        break;

                    case ASEQ_OP_SEQ_DYNCALL: // seqPlayer: dyncall
                        temp = AudioScript_ScriptReadS16(seqScript);
                        if ((seqScript->value != -1) && (seqScript->depth != 3)) {
                            data1 = seqPlayer->seqData + (u32)(temp + (seqScript->value << 1));
                            seqScript->stack[seqScript->depth] = seqScript->pc;
                            seqScript->depth++;
                            temp = (data1[0] << 8) + data1[1];
                            seqScript->pc = &seqPlayer->seqData[temp];
                        }
                        break;

                    case ASEQ_OP_SEQ_LDI: // seqPlayer: set value
                        seqScript->value = AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_AND: // seqPlayer: `bit and` -> set value
                        seqScript->value &= AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_SUB: // seqPlayer: subtract -> set value
                        seqScript->value -= AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_STSEQ: // seqPlayer: write into sequence script
                        cmd = AudioScript_ScriptReadU8(seqScript);
                        temp = AudioScript_ScriptReadS16(seqScript);
                        data2 = &seqPlayer->seqData[temp];
                        *data2 = (u8)seqScript->value + cmd;
                        break;

                    case ASEQ_OP_SEQ_C2: // seqPlayer:
                        temp = AudioScript_ScriptReadS16(seqScript);
                        if (seqScript->value != -1) {
                            data4 = seqPlayer->seqData + (u32)(temp + (seqScript->value << 1));

                            temp = (data4[0] << 8) + data4[1];
                            seqScript->pc = &seqPlayer->seqData[temp];
                        }
                        break;

                    case ASEQ_OP_SEQ_STOP: // seqPlayer: stop script
                        seqPlayer->stopScript = true;
                        return;

                    case ASEQ_OP_SEQ_SCRIPTCTR: // seqPlayer:
                        seqPlayer->unk_16 = AudioScript_ScriptReadS16(seqScript);
                        break;

                    case ASEQ_OP_SEQ_EF: // seqPlayer:
                        AudioScript_ScriptReadS16(seqScript);
                        AudioScript_ScriptReadU8(seqScript);
                        break;

                    case ASEQ_OP_SEQ_RUNSEQ: // seqPlayer: start sequence
                        cmd = AudioScript_ScriptReadU8(seqScript);
                        if (cmd == 0xFF) {
                            cmd = seqPlayer->playerIndex;
                            if (seqPlayer->state == SEQPLAYER_STATE_FADE_OUT) {
                                break;
                            }
                        }

                        cmdLowBits = AudioScript_ScriptReadU8(seqScript);
                        AudioLoad_SyncInitSeqPlayer(cmd, cmdLowBits, 0);
                        if (cmd == (u8)seqPlayer->playerIndex) {
                            return;
                        }
                        break;

                    case ASEQ_OP_SEQ_C3: // seqPlayer:
                        temp = AudioScript_ScriptReadS16(seqScript);
                        if (seqScript->value != -1) {
                            new_var = (u16*)(seqPlayer->seqData + (u32)(temp + seqScript->value * 2));
                            temp = *new_var;

                            for (i = 0; i < ARRAY_COUNT(seqPlayer->channels); i++) {
                                seqPlayer->channels[i]->muted = temp & 1;
                                temp = temp >> 1;
                            }
                        }
                        break;
                }
                continue;
            }

            // Commands 0x00 - 0xBF
            cmdLowBits = cmd & 0x0F;

            switch (cmd & 0xF0) {
                case ASEQ_OP_SEQ_TESTCHAN: // seqPlayer: test channel disabled
                    seqScript->value = seqPlayer->channels[cmdLowBits]->enabled ^ 1;
                    break;

                case ASEQ_OP_SEQ_SUBIO: // seqPlayer: io read value subtract
                    seqScript->value -= seqPlayer->seqScriptIO[cmdLowBits];
                    break;

                case ASEQ_OP_SEQ_STIO: // seqPlayer: io write value
                    seqPlayer->seqScriptIO[cmdLowBits] = seqScript->value;
                    break;

                case ASEQ_OP_SEQ_LDIO: // seqPlayer: io read value
                    seqScript->value = seqPlayer->seqScriptIO[cmdLowBits];
                    if (cmdLowBits < 2) {
                        seqPlayer->seqScriptIO[cmdLowBits] = SEQ_IO_VAL_NONE;
                    }
                    break;

                case ASEQ_OP_SEQ_STOPCHAN: // seqPlayer: disable channel
                    AudioScript_SequenceChannelDisable(seqPlayer->channels[cmdLowBits]);
                    break;

                case ASEQ_OP_SEQ_LDCHAN: // seqPlayer: start channel
                    temp = AudioScript_ScriptReadS16(seqScript);
                    AudioScript_SequenceChannelEnable(seqPlayer, cmdLowBits, (void*)&seqPlayer->seqData[temp]);
                    break;

                case ASEQ_OP_SEQ_RLDCHAN: // seqPlayer: start channel relative
                    tempS = AudioScript_ScriptReadS16(seqScript);
                    AudioScript_SequenceChannelEnable(seqPlayer, cmdLowBits, (void*)&seqScript->pc[tempS]);
                    break;

                case ASEQ_OP_SEQ_LDSEQ: // seqPlayer: load sequence
                    cmd = AudioScript_ScriptReadU8(seqScript);
                    temp = AudioScript_ScriptReadS16(seqScript);
                    data2 = &seqPlayer->seqData[temp];
                    AudioLoad_SlowLoadSeq(cmd, data2, &seqPlayer->seqScriptIO[cmdLowBits]);
                    break;

                case ASEQ_OP_SEQ_LDRES: // seqPlayer: async load
                    cmd = AudioScript_ScriptReadU8(seqScript);
                    value = cmd;
                    temp = AudioScript_ScriptReadU8(seqScript);
                    AudioLoad_ScriptLoad(value, temp, &seqPlayer->seqScriptIO[cmdLowBits]);
                    break;
            }
        }
    }

    for (j = 0; j < SEQ_NUM_CHANNELS; j++) {
        channel = seqPlayer->channels[j];
        if (channel->enabled) {
            AudioScript_SequenceChannelProcessScript(channel);
        }
    }
}

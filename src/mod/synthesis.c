#include "global.h"
#include "modding.h"
#include "init.h"
#include "heap.h"
#include "load.h"
#include "effects.h"
#include "util.h"

/**
 * This file adds full support for playing PCM-16 (PCM signed 16-bit big-endian) files. Some support
 * for this codec is present in the vanilla ROM, but the code is incomplete.
 *
 * You can encode your files for PCM-16 using either Audacity or ffmpeg.
 *
 * Audacity
 * ========
 * When exporting, first select "Other uncompressed files". Then select these options:
 *   Mono, 32000 Hz, "RAW (header-less)", and "Signed 16-bit PCM"
 *
 * Export the file with a name such as "my-sample.raw" and then use GNU binutils to byte-swap:
 *   objcopy -I binary -O binary --reverse-bytes=2 path/to/my-sample.raw
 *
 * ffmpeg
 * ======
 * Use a command similar to this:
 *   ffmpeg -i input.wav -acodec pcm_s16be -f s16be -ac 1 -ar 32000 my-sample.raw
 *
 */

// DMEM Addresses for the RSP
#define DMEM_TEMP 0x3B0
#define DMEM_TEMP2 0x3C0
#define DMEM_SURROUND_TEMP 0x4B0
#define DMEM_UNCOMPRESSED_NOTE 0x570
#define DMEM_HAAS_TEMP 0x5B0
#define DMEM_COMB_TEMP 0x750             // = DMEM_TEMP + DMEM_2CH_SIZE + a bit more
#define DMEM_COMPRESSED_ADPCM_DATA 0x930 // = DMEM_LEFT_CH
#define DMEM_LEFT_CH 0x930
#define DMEM_RIGHT_CH 0xAD0
#define DMEM_WET_TEMP 0x3D0
#define DMEM_WET_SCRATCH 0x710 // = DMEM_WET_TEMP + DMEM_2CH_SIZE
#define DMEM_WET_LEFT_CH 0xC70
#define DMEM_WET_RIGHT_CH 0xE10 // = DMEM_WET_LEFT_CH + DMEM_1CH_SIZE

typedef enum {
    /* 0 */ HAAS_EFFECT_DELAY_NONE,
    /* 1 */ HAAS_EFFECT_DELAY_LEFT, // Delay left channel so that right channel is heard first
    /* 2 */ HAAS_EFFECT_DELAY_RIGHT // Delay right channel so that left channel is heard first
} HaasEffectDelaySide;

void AudioSynth_SyncSampleStates(s32 updateIndex);
void AudioSynth_AddReverbBufferEntry(s32 numSamples, s32 updateIndex, s32 reverbIndex);
Acmd* AudioSynth_SaveReverbSamples(Acmd* cmd, SynthesisReverb* reverb, s16 updateIndex);
Acmd* AudioSynth_SaveSubReverbSamples(Acmd* cmd, SynthesisReverb* reverb, s16 updateIndex);
Acmd* AudioSynth_FilterReverb(Acmd* cmd, s32 size, SynthesisReverb* reverb);
Acmd* AudioSynth_LeakReverb(Acmd* cmd, SynthesisReverb* reverb);
Acmd* AudioSynth_MixOtherReverbIndex(Acmd* cmd, SynthesisReverb* reverb, s32 updateIndex);
Acmd* AudioSynth_LoadReverbSamples(Acmd* cmd, s32 numSamplesPerUpdate, SynthesisReverb* reverb, s16 updateIndex);
Acmd* AudioSynth_LoadSubReverbSamplesWithoutDownsample(Acmd* cmd, s32 numSamplesPerUpdate, SynthesisReverb* reverb,
                                                       s16 updateIndex);
Acmd* AudioSynth_ProcessSamples(s16* aiBuf, s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex);
Acmd* AudioSynth_ProcessSample(s32 noteIndex, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                               s16* aiBuf, s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex);
Acmd* AudioSynth_ApplySurroundEffect(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                     s32 numSamplesPerUpdate, s32 haasDmem, s32 flags);
Acmd* AudioSynth_FinalResample(Acmd* cmd, NoteSynthesisState* synthState, s32 size, u16 pitch, u16 inpDmem,
                               s32 resampleFlags);
Acmd* AudioSynth_ProcessEnvelope(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                 s32 numSamplesPerUpdate, u16 dmemSrc, s32 haasEffectDelaySide, s32 flags);
Acmd* AudioSynth_LoadWaveSamples(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                 s32 numSamplesToLoad);
Acmd* AudioSynth_ApplyHaasEffect(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                 s32 size, s32 flags, s32 haasEffectDelaySide);
void AudioSynth_LoadBuffer(Acmd* cmd, s32 dmemDest, s32 size, void* addrSrc);
void AudioSynth_ClearBuffer(Acmd* cmd, s32 dmem, s32 size);
void AudioSynth_SetBuffer(Acmd* cmd, s32 flags, s32 dmemIn, s32 dmemOut, size_t size);
void AudioSynth_S8Dec(Acmd* cmd, s32 flags, s16* state);
void AudioSynth_Mix(Acmd* cmd, size_t size, s32 gain, s32 dmemIn, s32 dmemOut);
void AudioSynth_DisableSampleStates(s32 updateIndex, s32 noteIndex);
void AudioSynth_InterL(Acmd* cmd, s32 dmemIn, s32 dmemOut, s32 numSamples);
void AudioSynth_HiLoGain(Acmd* cmd, s32 gain, s32 dmemIn, s32 dmemOut, s32 size);
void AudioSynth_UnkCmd19(Acmd* cmd, s32 dmem1, s32 dmem2, s32 size, s32 arg4);
void AudioSynth_LoadFilterBuffer(Acmd* cmd, s32 flags, s32 buf, s16* addr);
void AudioSynth_DMemMove(Acmd* cmd, s32 dmemIn, s32 dmemOut, size_t size);
void AudioSynth_SaveBuffer(Acmd* cmd, s32 dmemSrc, s32 size, void* addrDest);
void AudioSynth_LoadFilterSize(Acmd* cmd, size_t size, s16* addr);

RECOMP_PATCH Acmd* AudioSynth_Update(Acmd* abiCmdStart, s32* numAbiCmds, s16* aiBufStart, s32 numSamplesPerFrame) {
    s32 numSamplesPerUpdate;
    s16* curAiBufPos;
    Acmd* curCmd = abiCmdStart;
    s32 updateIndex;
    s32 reverseUpdateIndex;
    s32 reverbIndex;
    SynthesisReverb* reverb;

    // @mod Zelda 64's audio engine is intrinsically linked to the output rate in a way that changes
    // the result if the output rate is increased to 48kHz. The main reason is that the game limits
    // the number of samples process per update to around 200 samples per update. This is necessary
    // since DMEM is limited. For each update, a call to the AudioScript_ProcessSequences() function
    // is made. At 32kHz the game will run three updates per frame, and at 48kHz it will run four.
    // This causes problems because many of counters in the sequence script are not scaled with the
    // increased output rate. For example, `delay 12` would finish in four frames at 32kHz, but only
    // three frames at 48kHz. The solution is that we must still update the sequence player three
    // times per frame, but then process each update in two parts. This means that there will be six
    // updates per frame. This means more RSP commands, which may have been a problem on original
    // hardware, but shouldn't cause any issues on modern machines.
    //
    // Additionally, the number of samples process per frame must be carefully calculated to be as
    // close to the original 32kHz numbers as possible, then scaled up after the calculation is done.

    for (reverseUpdateIndex = gAudioCtx.audioBufferParameters.updatesPerFrame; reverseUpdateIndex > 0;
         reverseUpdateIndex--) {
        AudioScript_ProcessSequences(reverseUpdateIndex - 1);
        AudioSynth_SyncSampleStates(gAudioCtx.audioBufferParameters.updatesPerFrame - reverseUpdateIndex);
    }

    curAiBufPos = aiBufStart;
    gAudioCtx.adpcmCodeBook = NULL;

    numSamplesPerFrame = (gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget / FREQ_FACTOR) -
        ROUND((f32)osAiGetLength() / (FREQ_FACTOR * 2 * SAMPLE_SIZE));

    numSamplesPerFrame =
        (s16)((((numSamplesPerFrame + 8 * SAMPLES_PER_FRAME) & ~0xF) + SAMPLES_PER_FRAME) * FREQ_FACTOR);

    numSamplesPerFrame = gAudioCtx.numSamplesPerFrame[gAudioCtx.curAiBufferIndex] =
        CLAMP(numSamplesPerFrame, gAudioCtx.audioBufferParameters.numSamplesPerFrameMin,
              gAudioCtx.audioBufferParameters.numSamplesPerFrameMax);

    // Process/Update all samples multiple times in a single frame
    for (updateIndex = 0; updateIndex < gAudioCtx.audioBufferParameters.updatesPerFrame * NUM_SUB_UPDATES;
         updateIndex++) {
        reverseUpdateIndex = gAudioCtx.audioBufferParameters.updatesPerFrame * NUM_SUB_UPDATES - updateIndex;

        if (reverseUpdateIndex == 1) {
            // Final Update
            numSamplesPerUpdate = numSamplesPerFrame;
        } else {
            numSamplesPerUpdate =
                CLAMP(numSamplesPerFrame / reverseUpdateIndex, gAudioCtx.audioBufferParameters.numSamplesPerUpdateMin,
                      gAudioCtx.audioBufferParameters.numSamplesPerUpdateMax);
            if (numSamplesPerUpdate & 7) {
                numSamplesPerUpdate = ALIGN8(numSamplesPerUpdate) - (updateIndex & 1) * 8;
            }
        }

        for (reverbIndex = 0; reverbIndex < gAudioCtx.numSynthesisReverbs; reverbIndex++) {
            if (gAudioCtx.synthesisReverbs[reverbIndex].useReverb) {
                AudioSynth_AddReverbBufferEntry(numSamplesPerUpdate, updateIndex, reverbIndex);
            }
        }

        curCmd = AudioSynth_ProcessSamples(curAiBufPos, numSamplesPerUpdate, curCmd, updateIndex);

        numSamplesPerFrame -= numSamplesPerUpdate;
        curAiBufPos += numSamplesPerUpdate * SAMPLE_SIZE;
    }

    // Update reverb frame info
    for (reverbIndex = 0; reverbIndex < gAudioCtx.numSynthesisReverbs; reverbIndex++) {
        if (gAudioCtx.synthesisReverbs[reverbIndex].framesToIgnore != 0) {
            gAudioCtx.synthesisReverbs[reverbIndex].framesToIgnore--;
        }
        gAudioCtx.synthesisReverbs[reverbIndex].curFrame ^= 1;
    }

    *numAbiCmds = curCmd - abiCmdStart;
    return curCmd;
}

/**
 * Process all samples embedded in a note. Every sample has numSamplesPerUpdate processed,
 * and each of those are mixed together into both DMEM_LEFT_CH and DMEM_RIGHT_CH
 */
RECOMP_PATCH Acmd* AudioSynth_ProcessSamples(s16* aiBuf, s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex) {
    s32 size;
    u8 noteIndices[0x58];
    s16 noteCount = 0;
    s16 reverbIndex;
    SynthesisReverb* reverb;
    s32 useReverb;
    s32 i;

    // @mod Each seqplayer update is processed in two parts, so updateIndex needs to be scaled back down.
    // This is the only change in this function.
    s32 sampleStateOffset = gAudioCtx.numNotes * (updateIndex / NUM_SUB_UPDATES);

    if (gAudioCtx.numSynthesisReverbs == 0) {
        for (i = 0; i < gAudioCtx.numNotes; i++) {
            if (gAudioCtx.sampleStateList[sampleStateOffset + i].bitField0.enabled) {
                noteIndices[noteCount++] = i;
            }
        }
    } else {
        NoteSampleState* sampleState;

        for (reverbIndex = 0; reverbIndex < gAudioCtx.numSynthesisReverbs; reverbIndex++) {
            for (i = 0; i < gAudioCtx.numNotes; i++) {
                sampleState = &gAudioCtx.sampleStateList[sampleStateOffset + i];
                if (sampleState->bitField0.enabled && (sampleState->bitField1.reverbIndex == reverbIndex)) {
                    noteIndices[noteCount++] = i;
                }
            }
        }

        for (i = 0; i < gAudioCtx.numNotes; i++) {
            sampleState = &gAudioCtx.sampleStateList[sampleStateOffset + i];
            if (sampleState->bitField0.enabled &&
                (sampleState->bitField1.reverbIndex >= gAudioCtx.numSynthesisReverbs)) {
                noteIndices[noteCount++] = i;
            }
        }
    }

    aClearBuffer(cmd++, DMEM_LEFT_CH, DMEM_2CH_SIZE);

    i = 0;
    for (reverbIndex = 0; reverbIndex < gAudioCtx.numSynthesisReverbs; reverbIndex++) {
        s32 subDelay;
        NoteSampleState* sampleState;

        reverb = &gAudioCtx.synthesisReverbs[reverbIndex];
        useReverb = reverb->useReverb;
        if (useReverb) {

            // Loads reverb samples from DRAM (ringBuffer) into DMEM (DMEM_WET_LEFT_CH)
            cmd = AudioSynth_LoadReverbSamples(cmd, numSamplesPerUpdate, reverb, updateIndex);

            // Mixes reverb sample into the main dry channel
            // reverb->volume is always set to 0x7FFF (audio spec), and DMEM_LEFT_CH is cleared before reverbs.
            // So this is essentially a DMEMmove from DMEM_WET_LEFT_CH to DMEM_LEFT_CH
            aMix(cmd++, DMEM_2CH_SIZE >> 4, reverb->volume, DMEM_WET_LEFT_CH, DMEM_LEFT_CH);

            subDelay = reverb->subDelay;
            if (subDelay != 0) {
                aDMEMMove(cmd++, DMEM_WET_LEFT_CH, DMEM_WET_TEMP, DMEM_2CH_SIZE);
            }

            // Decays reverb over time. The (+ 0x8000) here is -100%
            aMix(cmd++, DMEM_2CH_SIZE >> 4, reverb->decayRatio + 0x8000, DMEM_WET_LEFT_CH, DMEM_WET_LEFT_CH);

            if (((reverb->leakRtl != 0) || (reverb->leakLtr != 0)) && (gAudioCtx.soundMode != SOUNDMODE_MONO)) {
                cmd = AudioSynth_LeakReverb(cmd, reverb);
            }

            if (subDelay != 0) {
                if (reverb->mixReverbIndex != REVERB_INDEX_NONE) {
                    cmd = AudioSynth_MixOtherReverbIndex(cmd, reverb, updateIndex);
                }
                cmd = AudioSynth_SaveReverbSamples(cmd, reverb, updateIndex);
                cmd = AudioSynth_LoadSubReverbSamplesWithoutDownsample(cmd, numSamplesPerUpdate, reverb, updateIndex);
                aMix(cmd++, DMEM_2CH_SIZE >> 4, reverb->subVolume, DMEM_WET_TEMP, DMEM_WET_LEFT_CH);
            }
        }

        while (i < noteCount) {
            sampleState = &gAudioCtx.sampleStateList[sampleStateOffset + noteIndices[i]];
            if (sampleState->bitField1.reverbIndex != reverbIndex) {
                break;
            }
            cmd = AudioSynth_ProcessSample(noteIndices[i], sampleState,
                                           &gAudioCtx.notes[noteIndices[i]].synthesisState,
                                           aiBuf, numSamplesPerUpdate, cmd, updateIndex);
            i++;
        }

        if (useReverb) {
            if ((reverb->filterLeft != NULL) || (reverb->filterRight != NULL)) {
                cmd = AudioSynth_FilterReverb(cmd, numSamplesPerUpdate * SAMPLE_SIZE, reverb);
            }

            // Saves the wet channel sample from DMEM (DMEM_WET_LEFT_CH) into (ringBuffer) DRAM for future use
            if (subDelay != 0) {
                cmd = AudioSynth_SaveSubReverbSamples(cmd, reverb, updateIndex);
            } else {
                if (reverb->mixReverbIndex != REVERB_INDEX_NONE) {
                    cmd = AudioSynth_MixOtherReverbIndex(cmd, reverb, updateIndex);
                }
                cmd = AudioSynth_SaveReverbSamples(cmd, reverb, updateIndex);
            }
        }
    }

    while (i < noteCount) {
        cmd = AudioSynth_ProcessSample(noteIndices[i], &gAudioCtx.sampleStateList[sampleStateOffset + noteIndices[i]],
                                       &gAudioCtx.notes[noteIndices[i]].synthesisState, aiBuf, numSamplesPerUpdate,
                                       cmd, updateIndex);
        i++;
    }

    size = numSamplesPerUpdate * SAMPLE_SIZE;
    aInterleave(cmd++, DMEM_TEMP, DMEM_LEFT_CH, DMEM_RIGHT_CH, size);

    if (gAudioCustomSynthFunction != NULL) {
        cmd = gAudioCustomSynthFunction(cmd, 2 * size, updateIndex);
    }
    aSaveBuffer(cmd++, DMEM_TEMP, aiBuf, 2 * size);

    return cmd;
}

__attribute__((optnone))
RECOMP_PATCH Acmd* AudioSynth_ProcessSample(s32 noteIndex, NoteSampleState* sampleState,
                                            NoteSynthesisState* synthState, s16* aiBuf,
                                            s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex) {
    s32 pad1[2];
    void* reverbAddrSrc;
    Sample* sample;
    AdpcmLoop* loopInfo;
    s32 numSamplesUntilEnd;
    s32 numSamplesInThisIteration;
    s32 sampleFinished;
    s32 loopToPoint;
    s32 flags;
    u16 frequencyFixedPoint;
    s32 gain;
    s32 frameIndex;
    s32 skipBytes;
    s32 numSamplesToDecode;
    s32 numFirstFrameSamplesToIgnore;
    u8* sampleAddr;
    u32 numSamplesToLoadFixedPoint;
    s32 numSamplesToLoadAdj;
    s32 numSamplesProcessed;
    s32 sampleEndPos;
    s32 numSamplesToProcess;
    s32 dmemUncompressedAddrOffset2;
    s32 pad2[3];
    s32 numSamplesInFirstFrame;
    s32 numTrailingSamplesToIgnore;
    s32 pad3[3];
    s32 frameSize;
    s32 numFramesToDecode;
    s32 skipInitialSamples;
    s32 zeroOffset;
    u8* samplesToLoadAddr;
    s32 numParts;
    s32 curPart;
    s32 sampleDataChunkAlignPad;
    s32 haasEffectDelaySide;
    s32 numSamplesToLoadFirstPart;
    u16 sampleDmemBeforeResampling;
    s32 sampleAddrOffset;
    s32 dmemUncompressedAddrOffset1;
    Note* note;
    u32 numSamplesToLoad;
    s16* filter;
    s32 bookOffset = sampleState->bitField1.bookOffset;
    s32 finished = sampleState->bitField0.finished;
    s32 sampleDataChunkSize;
    s16 sampleDataDmemAddr;

    note = &gAudioCtx.notes[noteIndex];
    flags = A_CONTINUE;

    // Initialize the synthesis state
    if (sampleState->bitField0.needsInit == true) {
        flags = A_INIT;
        synthState->atLoopPoint = false;
        synthState->stopLoop = false;
        synthState->samplePosInt = note->playbackState.startSamplePos;
        synthState->samplePosFrac = 0;
        synthState->curVolLeft = 0;
        synthState->curVolRight = 0;
        synthState->prevHaasEffectLeftDelaySize = 0;
        synthState->prevHaasEffectRightDelaySize = 0;
        synthState->curReverbVol = sampleState->targetReverbVol;
        synthState->numParts = 0;
        synthState->combFilterNeedsInit = true;
        note->sampleState.bitField0.finished = false;
        synthState->unk_1F = note->playbackState.unk_80; // Never set, never used
        finished = false;
    }

    // Process the sample in either one or two parts
    numParts = sampleState->bitField1.hasTwoParts + 1;

    // Determine number of samples to load based on numSamplesPerUpdate and relative frequency
    frequencyFixedPoint = sampleState->frequencyFixedPoint;
    numSamplesToLoadFixedPoint = (frequencyFixedPoint * numSamplesPerUpdate * 2) + synthState->samplePosFrac;
    numSamplesToLoad = numSamplesToLoadFixedPoint >> 16;

    if (numSamplesToLoad == 0) {
        skipBytes = false;
    }

    synthState->samplePosFrac = numSamplesToLoadFixedPoint & 0xFFFF;

    // Partially-optimized out no-op ifs required for matching. SM64 decomp
    // makes it clear that this is how it should look.
    if ((synthState->numParts == 1) && (numParts == 2)) {
    } else if ((synthState->numParts == 2) && (numParts == 1)) {
    } else {
    }

    synthState->numParts = numParts;

    if (sampleState->bitField1.isSyntheticWave) {
        cmd = AudioSynth_LoadWaveSamples(cmd, sampleState, synthState, numSamplesToLoad);
        sampleDmemBeforeResampling = DMEM_UNCOMPRESSED_NOTE + (synthState->samplePosInt * 2);
        synthState->samplePosInt += numSamplesToLoad;
    } else {
        sample = sampleState->tunedSample->sample;
        loopInfo = sample->loop;

        if (note->playbackState.status != PLAYBACK_STATUS_0) {
            synthState->stopLoop = true;
        }

        if ((loopInfo->header.count == 2) && synthState->stopLoop) {
            sampleEndPos = loopInfo->header.sampleEnd;
        } else {
            sampleEndPos = loopInfo->header.loopEnd;
        }

        sampleAddr = sample->sampleAddr;
        numSamplesToLoadFirstPart = 0;

        // If the frequency requested is more than double that of the raw sample,
        // then the sample processing is split into two parts.
        for (curPart = 0; curPart < numParts; curPart++) {
            numSamplesProcessed = 0;
            dmemUncompressedAddrOffset1 = 0;

            // Adjust the number of samples to load only if there are two parts and an odd number of samples
            if (numParts == 1) {
                numSamplesToLoadAdj = numSamplesToLoad;
            } else if (numSamplesToLoad & 1) {
                // round down for the first part
                // round up for the second part
                numSamplesToLoadAdj = (numSamplesToLoad & ~1) + (curPart * 2);
            } else {
                numSamplesToLoadAdj = numSamplesToLoad;
            }

            // Load the ADPCM codeBook
            // @mod RSP can't read from mod memory, so move codebook into audio heap
            if ((sample->codec == CODEC_ADPCM) || (sample->codec == CODEC_SMALL_ADPCM)) {
                if (gAudioCtx.adpcmCodeBook != sample->book->codeBook) {
                    s16* codeBook;
                    u32 numEntries = SAMPLES_PER_FRAME * sample->book->header.order *
                        sample->book->header.numPredictors;

                    if (bookOffset == 1) {
                        gAudioCtx.adpcmCodeBook = &gInvalidAdpcmCodeBook[1];
                        codeBook = gAudioCtx.adpcmCodeBook;
                    } else {
                        gAudioCtx.adpcmCodeBook = sample->book->codeBook;
                        codeBook = IS_MOD_MEMORY(gAudioCtx.adpcmCodeBook)
                            ? AudioApi_RspCacheMemcpy(gAudioCtx.adpcmCodeBook, sizeof(s16) * numEntries)
                            : gAudioCtx.adpcmCodeBook;
                    }

                    aLoadADPCM(cmd++, numEntries, codeBook);
                }
            }

            // Continue processing samples until the number of samples needed to load is reached
            while (numSamplesProcessed != numSamplesToLoadAdj) {
                sampleFinished = false;
                loopToPoint = false;
                dmemUncompressedAddrOffset2 = 0;

                numFirstFrameSamplesToIgnore = synthState->samplePosInt & 0xF;
                numSamplesUntilEnd = sampleEndPos - synthState->samplePosInt;

                // Calculate number of samples to process this loop
                numSamplesToProcess = numSamplesToLoadAdj - numSamplesProcessed;

                if ((numFirstFrameSamplesToIgnore == 0) && !synthState->atLoopPoint) {
                    numFirstFrameSamplesToIgnore = SAMPLES_PER_FRAME;
                }
                numSamplesInFirstFrame = SAMPLES_PER_FRAME - numFirstFrameSamplesToIgnore;

                // Determine the number of samples to decode based on whether the end will be reached or not.
                if (numSamplesToProcess < numSamplesUntilEnd) {
                    // The end will not be reached.
                    numFramesToDecode =
                        (s32)(numSamplesToProcess - numSamplesInFirstFrame + SAMPLES_PER_FRAME - 1) / SAMPLES_PER_FRAME;
                    numSamplesToDecode = numFramesToDecode * SAMPLES_PER_FRAME;
                    numTrailingSamplesToIgnore = numSamplesInFirstFrame + numSamplesToDecode - numSamplesToProcess;
                } else {
                    // The end will be reached.
                    numSamplesToDecode = numSamplesUntilEnd - numSamplesInFirstFrame;
                    numTrailingSamplesToIgnore = 0;
                    if (numSamplesToDecode <= 0) {
                        numSamplesToDecode = 0;
                        numSamplesInFirstFrame = numSamplesUntilEnd;
                    }
                    numFramesToDecode = (numSamplesToDecode + SAMPLES_PER_FRAME - 1) / SAMPLES_PER_FRAME;
                    if (loopInfo->header.count != 0) {
                        if ((loopInfo->header.count == 2) && synthState->stopLoop) {
                            sampleFinished = true;
                        } else {
                            // Loop around and restart
                            loopToPoint = true;
                        }
                    } else {
                        sampleFinished = true;
                    }
                }

                // Set parameters based on compression type
                switch (sample->codec) {
                    case CODEC_ADPCM:
                        // 16 2-byte samples (32 bytes) compressed into 4-bit samples (8 bytes) + 1 header byte
                        frameSize = 9;
                        skipInitialSamples = SAMPLES_PER_FRAME;
                        zeroOffset = 0;
                        break;

                    case CODEC_SMALL_ADPCM:
                        // 16 2-byte samples (32 bytes) compressed into 2-bit samples (4 bytes) + 1 header byte
                        frameSize = 5;
                        skipInitialSamples = SAMPLES_PER_FRAME;
                        zeroOffset = 0;
                        break;

                    case CODEC_UNK7:
                        // 2 2-byte samples (4 bytes) processed without decompression
                        frameSize = 4;
                        skipInitialSamples = SAMPLES_PER_FRAME;
                        zeroOffset = 0;
                        break;

                    case CODEC_S8:
                        // 16 2-byte samples (32 bytes) compressed into 8-bit samples (16 bytes)
                        frameSize = 16;
                        skipInitialSamples = SAMPLES_PER_FRAME;
                        zeroOffset = 0;
                        break;

                    case CODEC_REVERB:
                        reverbAddrSrc = (void*)0xFFFFFFFF;
                        if (gAudioCustomReverbFunction != NULL) {
                            reverbAddrSrc = gAudioCustomReverbFunction(sample, numSamplesToLoadAdj,
                                                                       flags, noteIndex);
                        }

                        if (reverbAddrSrc == (void*)0xFFFFFFFF) {
                            sampleFinished = true;
                        } else if (reverbAddrSrc == NULL) {
                            return cmd;
                        } else {
                            AudioSynth_LoadBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE,
                                                  (numSamplesToLoadAdj + SAMPLES_PER_FRAME) * SAMPLE_SIZE,
                                                  reverbAddrSrc);
                            flags = A_CONTINUE;
                            skipBytes = 0;
                            numSamplesProcessed = numSamplesToLoadAdj;
                            dmemUncompressedAddrOffset1 = numSamplesToLoadAdj;
                        }
                        goto skip;

                    case CODEC_S16_INMEMORY:
                    case CODEC_UNK6:
                        AudioSynth_ClearBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE,
                                               (numSamplesToLoadAdj + SAMPLES_PER_FRAME) * SAMPLE_SIZE);
                        flags = A_CONTINUE;
                        skipBytes = 0;
                        numSamplesProcessed = numSamplesToLoadAdj;
                        dmemUncompressedAddrOffset1 = numSamplesToLoadAdj;
                        goto skip;

                    case CODEC_S16:
                        AudioSynth_ClearBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE,
                                               (numSamplesToLoadAdj + SAMPLES_PER_FRAME) * SAMPLE_SIZE);

                        // @mod add support for PCM 16
                        // Note that despite being able to load the exact number of samples requested,
                        // we load an extra 16 samples in order to prevent audio crackling.
                        // These extra samples overlap the samples requested in the next update.
                        sampleDataChunkSize =
                            MIN(numSamplesToLoadAdj + SAMPLES_PER_FRAME, numSamplesUntilEnd) * SAMPLE_SIZE;

                        if (IS_DMA_CALLBACK_DEV_ADDR(sampleAddr)) {
                            samplesToLoadAddr =
                                AudioLoad_DmaSampleData((uintptr_t)sampleAddr, sampleDataChunkSize / SAMPLE_SIZE,
                                                        synthState->samplePosInt,
                                                        &synthState->sampleDmaIndex, sample->medium);
                        } else {
                            sampleAddrOffset = synthState->samplePosInt * SAMPLE_SIZE;
                            samplesToLoadAddr =
                                AudioLoad_DmaSampleData((uintptr_t)(sampleAddr + sampleAddrOffset),
                                                        sampleDataChunkSize, flags,
                                                        &synthState->sampleDmaIndex, sample->medium);
                        }

                        if (samplesToLoadAddr) {
                            AudioSynth_LoadBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE, sampleDataChunkSize, samplesToLoadAddr);
                        }

                        flags = A_CONTINUE;
                        skipBytes = 0;
                        numSamplesProcessed = numSamplesToLoadAdj;
                        dmemUncompressedAddrOffset1 = numSamplesToLoadAdj;
                        goto skip;

                    default:
                        break;
                }

                // Move the compressed raw sample data from ram into the rsp (DMEM)
                if (numFramesToDecode != 0) {
                    // Get the offset from the start of the sample to where the sample is currently playing from
                    frameIndex = (synthState->samplePosInt + skipInitialSamples - numFirstFrameSamplesToIgnore) /
                                 SAMPLES_PER_FRAME;
                    sampleAddrOffset = frameIndex * frameSize;

                    // Get the ram address of the requested sample chunk
                    if (sample->medium == MEDIUM_RAM) {
                        // Sample is already loaded into ram
                        samplesToLoadAddr = sampleAddr + (zeroOffset + sampleAddrOffset);
                    } else if (gAudioCtx.unk_29B8) { // always false
                        return cmd;
                    } else if (sample->medium == MEDIUM_UNK) {
                        // This medium is unsupported so terminate processing this note
                        return cmd;
                    } else {
                        // This medium is not in ram, so dma the requested sample into ram
                        samplesToLoadAddr =
                            AudioLoad_DmaSampleData((uintptr_t)(sampleAddr + (zeroOffset + sampleAddrOffset)),
                                                    ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME),
                                                    flags, &synthState->sampleDmaIndex, sample->medium);
                    }

                    if (samplesToLoadAddr == NULL) {
                        // The ram address was unsuccessfully allocated
                        return cmd;
                    }

                    // Move the raw sample chunk from ram to the rsp
                    // DMEM at the addresses before DMEM_COMPRESSED_ADPCM_DATA
                    sampleDataChunkAlignPad = (u32)samplesToLoadAddr & 0xF;
                    sampleDataChunkSize = ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME);
                    sampleDataDmemAddr = DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
                    aLoadBuffer(cmd++, samplesToLoadAddr - sampleDataChunkAlignPad, sampleDataDmemAddr,
                                sampleDataChunkSize);
                } else {
                    numSamplesToDecode = 0;
                    sampleDataChunkAlignPad = 0;
                }

                if (synthState->atLoopPoint) {
                    // @mod RSP can't read from mod memory, so move loop into audio heap
                    aSetLoop(cmd++, IS_MOD_MEMORY(sample->loop->predictorState)
                             ? AudioApi_RspCacheMemcpy(sample->loop->predictorState, sizeof(s16) * 16)
                             : sample->loop->predictorState);
                    flags = A_LOOP;
                    synthState->atLoopPoint = false;
                }

                numSamplesInThisIteration = numSamplesToDecode + numSamplesInFirstFrame - numTrailingSamplesToIgnore;

                if (numSamplesProcessed == 0) {
                    //! FAKE:
                    if (1) {}
                    skipBytes = numFirstFrameSamplesToIgnore * SAMPLE_SIZE;
                } else {
                    dmemUncompressedAddrOffset2 = ALIGN16(dmemUncompressedAddrOffset1 + 8 * SAMPLE_SIZE);
                }

                // Decompress the raw sample chunks in the rsp
                // Goes from adpcm (compressed) sample data to pcm (uncompressed) sample data
                switch (sample->codec) {
                    case CODEC_ADPCM:
                        sampleDataChunkSize = ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME);
                        sampleDataDmemAddr = DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
                        aSetBuffer(cmd++, 0, sampleDataDmemAddr + sampleDataChunkAlignPad,
                                   DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2,
                                   numSamplesToDecode * SAMPLE_SIZE);
                        aADPCMdec(cmd++, flags, synthState->synthesisBuffers->adpcmState);
                        break;

                    case CODEC_SMALL_ADPCM:
                        sampleDataChunkSize = ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME);
                        sampleDataDmemAddr = DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
                        aSetBuffer(cmd++, 0, sampleDataDmemAddr + sampleDataChunkAlignPad,
                                   DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2,
                                   numSamplesToDecode * SAMPLE_SIZE);
                        aADPCMdec(cmd++, flags | A_ADPCM_SHORT, synthState->synthesisBuffers->adpcmState);
                        break;

                    case CODEC_S8:
                        sampleDataChunkSize = ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME);
                        sampleDataDmemAddr = DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
                        AudioSynth_SetBuffer(cmd++, 0, sampleDataDmemAddr + sampleDataChunkAlignPad,
                                             DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2,
                                             numSamplesToDecode * SAMPLE_SIZE);
                        AudioSynth_S8Dec(cmd++, flags, synthState->synthesisBuffers->adpcmState);
                        break;

                    case CODEC_UNK7:
                    default:
                        // No decompression
                        break;
                }

                if (numSamplesProcessed != 0) {
                    aDMEMMove(cmd++,
                              DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2 +
                                  (numFirstFrameSamplesToIgnore * SAMPLE_SIZE),
                              DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset1,
                              numSamplesInThisIteration * SAMPLE_SIZE);
                }

                numSamplesProcessed += numSamplesInThisIteration;

                switch (flags) {
                    case A_INIT:
                        skipBytes = SAMPLES_PER_FRAME * SAMPLE_SIZE;
                        dmemUncompressedAddrOffset1 = (numSamplesToDecode + SAMPLES_PER_FRAME) * SAMPLE_SIZE;
                        break;

                    case A_LOOP:
                        dmemUncompressedAddrOffset1 =
                            numSamplesInThisIteration * SAMPLE_SIZE + dmemUncompressedAddrOffset1;
                        break;

                    default:
                        if (dmemUncompressedAddrOffset1 != 0) {
                            dmemUncompressedAddrOffset1 =
                                numSamplesInThisIteration * SAMPLE_SIZE + dmemUncompressedAddrOffset1;
                        } else {
                            dmemUncompressedAddrOffset1 =
                                (numFirstFrameSamplesToIgnore + numSamplesInThisIteration) * SAMPLE_SIZE;
                        }
                        break;
                }

                flags = A_CONTINUE;

            skip:

                // Update what to do with the samples next
                if (sampleFinished) {
                    if ((numSamplesToLoadAdj - numSamplesProcessed) != 0) {
                        AudioSynth_ClearBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset1,
                                               (numSamplesToLoadAdj - numSamplesProcessed) * SAMPLE_SIZE);
                    }
                    finished = true;
                    note->sampleState.bitField0.finished = true;
                    AudioSynth_DisableSampleStates(updateIndex, noteIndex);
                    break; // break out of the for-loop
                } else if (loopToPoint) {
                    synthState->atLoopPoint = true;
                    synthState->samplePosInt = loopInfo->header.start;
                } else {
                    synthState->samplePosInt += numSamplesToProcess;
                }
            }

            switch (numParts) {
                case 1:
                    sampleDmemBeforeResampling = DMEM_UNCOMPRESSED_NOTE + skipBytes;
                    break;

                case 2:
                    switch (curPart) {
                        case 0:
                            AudioSynth_InterL(cmd++, DMEM_UNCOMPRESSED_NOTE + skipBytes,
                                              DMEM_TEMP + (SAMPLES_PER_FRAME * SAMPLE_SIZE),
                                              ALIGN8(numSamplesToLoadAdj / 2));
                            numSamplesToLoadFirstPart = numSamplesToLoadAdj;
                            sampleDmemBeforeResampling = DMEM_TEMP + (SAMPLES_PER_FRAME * SAMPLE_SIZE);
                            if (finished) {
                                AudioSynth_ClearBuffer(cmd++, sampleDmemBeforeResampling + numSamplesToLoadFirstPart,
                                                       numSamplesToLoadAdj + SAMPLES_PER_FRAME);
                            }
                            break;

                        case 1:
                            AudioSynth_InterL(cmd++, DMEM_UNCOMPRESSED_NOTE + skipBytes,
                                              DMEM_TEMP + (SAMPLES_PER_FRAME * SAMPLE_SIZE) +
                                              numSamplesToLoadFirstPart, ALIGN8(numSamplesToLoadAdj / 2));
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
            if (finished) {
                break;
            }
        }
    }

    // Update the flags for the signal processing below
    flags = A_CONTINUE;
    if (sampleState->bitField0.needsInit == true) {
        sampleState->bitField0.needsInit = false;
        flags = A_INIT;
    }

    // Resample the decompressed mono-signal to the correct pitch
    cmd = AudioSynth_FinalResample(cmd, synthState, numSamplesPerUpdate * SAMPLE_SIZE, frequencyFixedPoint,
                                   sampleDmemBeforeResampling, flags);

    // UnkCmd19 was removed from the audio microcode
    // This block performs no operation
    if (bookOffset == 3) {
        AudioSynth_UnkCmd19(cmd++, DMEM_TEMP, DMEM_TEMP, numSamplesPerUpdate * (s32)SAMPLE_SIZE, 0);
    }

    // Apply the gain to the mono-signal to adjust the volume
    gain = sampleState->gain;
    if (gain != 0) {
        // A gain of 0x10 (a UQ4.4 number) is equivalent to 1.0 and represents no volume change
        if (gain < 0x10) {
            gain = 0x10;
        }
        AudioSynth_HiLoGain(cmd++, gain, DMEM_TEMP, 0, (numSamplesPerUpdate + SAMPLES_PER_FRAME) * SAMPLE_SIZE);
    }

    // Apply the filter to the mono-signal
    filter = sampleState->filter;
    if (filter != 0) {
        AudioSynth_LoadFilterSize(cmd++, numSamplesPerUpdate * SAMPLE_SIZE, filter);
        AudioSynth_LoadFilterBuffer(cmd++, flags, DMEM_TEMP, synthState->synthesisBuffers->filterState);
    }

    // Apply the comb filter to the mono-signal by taking the signal with a small temporal offset,
    // and adding it back to itself
    cmd = AudioApi_ApplyCombFilter(cmd, sampleState, synthState, numSamplesPerUpdate);

    // Determine the behavior of the audio processing that leads to the haas effect
    if ((sampleState->haasEffectLeftDelaySize != 0) || (synthState->prevHaasEffectLeftDelaySize != 0)) {
        haasEffectDelaySide = HAAS_EFFECT_DELAY_LEFT;
    } else if ((sampleState->haasEffectRightDelaySize != 0) || (synthState->prevHaasEffectRightDelaySize != 0)) {
        haasEffectDelaySide = HAAS_EFFECT_DELAY_RIGHT;
    } else {
        haasEffectDelaySide = HAAS_EFFECT_DELAY_NONE;
    }

    // Apply an unknown effect based on the surround sound-mode
    if (gAudioCtx.soundMode == SOUNDMODE_SURROUND) {
        sampleState->targetVolLeft = sampleState->targetVolLeft >> 1;
        sampleState->targetVolRight = sampleState->targetVolRight >> 1;
        if (sampleState->surroundEffectIndex != 0xFF) {
            cmd = AudioSynth_ApplySurroundEffect(cmd, sampleState, synthState, numSamplesPerUpdate,
                                                 DMEM_TEMP, flags);
        }
    }

    // Split the mono-signal into left and right channels:
    // Both for dry signal (to go to the speakers now)
    // and for wet signal (to go to a reverb buffer to be stored, and brought back later to produce an echo)
    cmd = AudioSynth_ProcessEnvelope(cmd, sampleState, synthState, numSamplesPerUpdate, DMEM_TEMP,
                                     haasEffectDelaySide, flags);

    // Apply the haas effect by delaying either the left or the right channel by a small amount
    if (sampleState->bitField1.useHaasEffect) {
        if (!(flags & A_INIT)) {
            flags = A_CONTINUE;
        }
        cmd = AudioSynth_ApplyHaasEffect(cmd, sampleState, synthState, numSamplesPerUpdate * (s32)SAMPLE_SIZE,
                                         flags, haasEffectDelaySide);
    }

    return cmd;
}

RECOMP_PATCH void AudioSynth_LoadFilterSize(Acmd* cmd, size_t size, s16* addr) {
    // @mod RSP can't read from mod memory, so move filter into audio heap
    if (IS_MOD_MEMORY(addr)) {
        addr = AudioApi_RspCacheMemcpy(addr, size);
    }
    aFilter(cmd, 2, size, addr);
}

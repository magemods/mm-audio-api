#include "global.h"
#include "modding.h"
#include "recomputils.h"

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

Acmd* AudioSynth_SaveResampledReverbSamplesImpl(Acmd* cmd, u16 dmem, u16 size, uintptr_t startAddr);
Acmd* AudioSynth_LoadReverbSamplesImpl(Acmd* cmd, u16 dmem, u16 startPos, s32 size, SynthesisReverb* reverb);
Acmd* AudioSynth_SaveReverbSamplesImpl(Acmd* cmd, u16 dmem, u16 startPos, s32 size, SynthesisReverb* reverb);
Acmd* AudioSynth_ProcessSamples(s16* aiBuf, s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex);
Acmd* AudioSynth_ProcessSample(s32 noteIndex, NoteSampleState* sampleState, NoteSynthesisState* synthState, s16* aiBuf,
                               s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex);
Acmd* AudioSynth_ApplySurroundEffect(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                     s32 numSamplesPerUpdate, s32 haasDmem, s32 flags);
Acmd* AudioSynth_FinalResample(Acmd* cmd, NoteSynthesisState* synthState, s32 size, u16 pitch, u16 inpDmem,
                               s32 resampleFlags);
Acmd* AudioSynth_ProcessEnvelope(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                 s32 numSamplesPerUpdate, u16 dmemSrc, s32 haasEffectDelaySide, s32 flags);
Acmd* AudioSynth_LoadWaveSamples(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                                 s32 numSamplesToLoad);
Acmd* AudioSynth_ApplyHaasEffect(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState, s32 size,
                                 s32 flags, s32 haasEffectDelaySide);
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

__attribute__((optnone))
RECOMP_PATCH Acmd* AudioSynth_ProcessSample(s32 noteIndex, NoteSampleState* sampleState, NoteSynthesisState* synthState, s16* aiBuf, s32 numSamplesPerUpdate, Acmd* cmd, s32 updateIndex) {
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
    void* combFilterState;
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
    s32 combFilterDmem;
    s32 dmemUncompressedAddrOffset1;
    Note* note;
    u32 numSamplesToLoad;
    u16 combFilterSize;
    u16 combFilterGain;
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
            if ((sample->codec == CODEC_ADPCM) || (sample->codec == CODEC_SMALL_ADPCM)) {
                if (gAudioCtx.adpcmCodeBook != sample->book->codeBook) {
                    u32 numEntries;

                    switch (bookOffset) {
                        case 1:
                            gAudioCtx.adpcmCodeBook = &gInvalidAdpcmCodeBook[1];
                            break;

                        case 2:
                        case 3:
                        default:
                            gAudioCtx.adpcmCodeBook = sample->book->codeBook;
                            break;
                    }

                    numEntries = SAMPLES_PER_FRAME * sample->book->header.order * sample->book->header.numPredictors;
                    aLoadADPCM(cmd++, numEntries, gAudioCtx.adpcmCodeBook);
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
                            reverbAddrSrc = gAudioCustomReverbFunction(sample, numSamplesToLoadAdj, flags, noteIndex);
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
                        // @mod
                        sampleDataChunkSize = ALIGN16(numSamplesToLoadAdj * SAMPLE_SIZE);
                        samplesToLoadAddr =
                            AudioLoad_DmaSampleData((uintptr_t)(sampleAddr + synthState->samplePosInt * SAMPLE_SIZE),
                                                    sampleDataChunkSize, flags,
                                                    &synthState->sampleDmaIndex, sample->medium);

                        AudioSynth_LoadBuffer(cmd++, DMEM_UNCOMPRESSED_NOTE, sampleDataChunkSize, samplesToLoadAddr);

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
                                                    ALIGN16((numFramesToDecode * frameSize) + SAMPLES_PER_FRAME), flags,
                                                    &synthState->sampleDmaIndex, sample->medium);
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
                    aSetLoop(cmd++, sample->loop->predictorState);
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
                                              DMEM_TEMP + (SAMPLES_PER_FRAME * SAMPLE_SIZE) + numSamplesToLoadFirstPart,
                                              ALIGN8(numSamplesToLoadAdj / 2));
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
    combFilterSize = sampleState->combFilterSize;
    combFilterGain = sampleState->combFilterGain;
    combFilterState = synthState->synthesisBuffers->combFilterState;
    if ((combFilterSize != 0) && (sampleState->combFilterGain != 0)) {
        AudioSynth_DMemMove(cmd++, DMEM_TEMP, DMEM_COMB_TEMP, numSamplesPerUpdate * SAMPLE_SIZE);
        combFilterDmem = DMEM_COMB_TEMP - combFilterSize;
        if (synthState->combFilterNeedsInit) {
            AudioSynth_ClearBuffer(cmd++, combFilterDmem, combFilterSize);
            synthState->combFilterNeedsInit = false;
        } else {
            AudioSynth_LoadBuffer(cmd++, combFilterDmem, combFilterSize, combFilterState);
        }
        AudioSynth_SaveBuffer(cmd++, DMEM_TEMP + (numSamplesPerUpdate * SAMPLE_SIZE) - combFilterSize, combFilterSize,
                              combFilterState);
        AudioSynth_Mix(cmd++, (numSamplesPerUpdate * (s32)SAMPLE_SIZE) >> 4, combFilterGain, DMEM_COMB_TEMP,
                       combFilterDmem);
        AudioSynth_DMemMove(cmd++, combFilterDmem, DMEM_TEMP, numSamplesPerUpdate * SAMPLE_SIZE);
    } else {
        synthState->combFilterNeedsInit = true;
    }

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
            cmd = AudioSynth_ApplySurroundEffect(cmd, sampleState, synthState, numSamplesPerUpdate, DMEM_TEMP, flags);
        }
    }

    // Split the mono-signal into left and right channels:
    // Both for dry signal (to go to the speakers now)
    // and for wet signal (to go to a reverb buffer to be stored, and brought back later to produce an echo)
    cmd = AudioSynth_ProcessEnvelope(cmd, sampleState, synthState, numSamplesPerUpdate, DMEM_TEMP, haasEffectDelaySide,
                                     flags);

    // Apply the haas effect by delaying either the left or the right channel by a small amount
    if (sampleState->bitField1.useHaasEffect) {
        if (!(flags & A_INIT)) {
            flags = A_CONTINUE;
        }
        cmd = AudioSynth_ApplyHaasEffect(cmd, sampleState, synthState, numSamplesPerUpdate * (s32)SAMPLE_SIZE, flags,
                                         haasEffectDelaySide);
    }

    return cmd;
}

#include <core/effects.h>
#include <recomp/modding.h>
#include <core/init.h>

/**
 * This file is responsible for modifying effects to work correctly with 48kHz
 */

#define DMEM_TEMP 0x3B0
#define DMEM_COMB_TEMP 0x750

extern u16 gHaasEffectDelaySize[64];

extern ReverbSettings reverbSettings0[3];
extern ReverbSettings reverbSettings1[3];
extern ReverbSettings reverbSettings2[3];
extern ReverbSettings reverbSettings3[3];
extern ReverbSettings reverbSettings4[3];
extern ReverbSettings reverbSettings5[3];
extern ReverbSettings reverbSettings6[3];
extern ReverbSettings reverbSettings7[3];
extern ReverbSettings reverbSettings8[2];
extern ReverbSettings reverbSettings9[3];
extern ReverbSettings reverbSettingsA[3];
extern ReverbSettings reverbSettingsB[3];
extern ReverbSettings reverbSettingsC[3];
extern ReverbSettings reverbSettingsD[3];
extern ReverbSettings reverbSettingsE[3];
extern ReverbSettings reverbSettingsF[2];

ReverbSettings* gReverbSettingsTableFull[] = {
    reverbSettings0, reverbSettings1, reverbSettings2, reverbSettings3,
    reverbSettings4, reverbSettings5, reverbSettings6, reverbSettings7,
    reverbSettings8, reverbSettings9, reverbSettingsA, reverbSettingsB,
    reverbSettingsC, reverbSettingsD, reverbSettingsE, reverbSettingsF,
};

u8 gReverbSettingsTableCount[] = {
    3, 3, 3, 3, 3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3, 2,
};

void AudioSynth_DMemMove(Acmd* cmd, s32 dmemIn, s32 dmemOut, size_t size);
void AudioSynth_LoadBuffer(Acmd* cmd, s32 dmemDest, s32 size, void* addrSrc);
void AudioSynth_ClearBuffer(Acmd* cmd, s32 dmem, s32 size);
void AudioSynth_SaveBuffer(Acmd* cmd, s32 dmemSrc, s32 size, void* addrDest);
void AudioSynth_Mix(Acmd* cmd, size_t size, s32 gain, s32 dmemIn, s32 dmemOut);

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_EffectsInit() {
    s32 i, j;

    for (i = 0; i < ARRAY_COUNT(gHaasEffectDelaySize); i++) {
        gHaasEffectDelaySize[i] *= FREQ_FACTOR;
    }

    for (i = 0; i < ARRAY_COUNT(gReverbSettingsTableFull); i++) {
        for (j = 0; j < gReverbSettingsTableCount[i]; j++) {
            ReverbSettings* settings = &gReverbSettingsTableFull[i][j];
            settings->delayNumSamples *= FREQ_FACTOR;
            settings->subDelay *= FREQ_FACTOR;
        }
    }
}

Acmd* AudioApi_ApplyCombFilter(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                               s32 numSamplesPerUpdate) {
    // @mod Comb filter size is measured in the number of samples, which needs to be scaled up for
    // proper 48kHz support. However this makes the size not 16 byte aligned, so we need to modify
    // the calculations a bit.
    u16 combFilterSize = ALIGN16((u16)(sampleState->combFilterSize * FREQ_FACTOR));
    u16 combFilterAlign = (u16)(sampleState->combFilterSize * FREQ_FACTOR) & 0xF;
    u16 combFilterGain = sampleState->combFilterGain;
    void* combFilterState = synthState->synthesisBuffers->combFilterState;
    s32 combFilterDmem;

    if ((combFilterSize != 0) && (sampleState->combFilterGain != 0)) {
        // copy the current samples to dmem_comb_temp
        AudioSynth_DMemMove(cmd++, DMEM_TEMP, DMEM_COMB_TEMP, numSamplesPerUpdate * SAMPLE_SIZE);
        combFilterDmem = DMEM_COMB_TEMP - combFilterSize;
        if (synthState->combFilterNeedsInit) {
            AudioSynth_ClearBuffer(cmd++, combFilterDmem, combFilterSize);
            synthState->combFilterNeedsInit = false;
        } else {
            // copy last iterations end of samples to comb dmem
            AudioSynth_LoadBuffer(cmd++, combFilterDmem, combFilterSize, combFilterState);
        }
        // save the very end of the current samples
        AudioSynth_SaveBuffer(cmd++, DMEM_TEMP + (numSamplesPerUpdate * SAMPLE_SIZE) - combFilterSize,
                              combFilterSize, combFilterState);
        AudioSynth_Mix(cmd++, (numSamplesPerUpdate * (s32)SAMPLE_SIZE) >> 4, combFilterGain, DMEM_COMB_TEMP,
                       combFilterDmem + combFilterAlign);
        // move back to dmem_temp
        AudioSynth_DMemMove(cmd++, combFilterDmem + combFilterAlign, DMEM_TEMP, numSamplesPerUpdate * SAMPLE_SIZE);
    } else {
        synthState->combFilterNeedsInit = true;
    }

    return cmd;
}

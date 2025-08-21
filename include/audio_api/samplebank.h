#ifndef __AUDIO_API_SAMPLEBANK_H__
#define __AUDIO_API_SAMPLEBANK_H__

/*! \file audio_api/samplebank.h
    \version 0.5.0
    \brief Low level imports for working with samplebanks
 */

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddSampleBank(AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_ReplaceSampleBank(s32 bankId, AudioTableEntry* entry));
RECOMP_IMPORT("magemods_audio_api", void AudioApi_RestoreSampleBank(s32 bankId));

#endif

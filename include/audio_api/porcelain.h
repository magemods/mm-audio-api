#ifndef __AUDIO_API_PORCELAIN_H__
#define __AUDIO_API_PORCELAIN_H__

/*! \file audio_api/porcelain.h
    \version 0.5.0
    \brief High level imports
 */

#include "types.h"

RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddResourceFromFs(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddSequenceFromFs(AudioApiSequenceInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddSoundFontFromFs(AudioApiSoundFontInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddSampleBankFromFs(AudioApiSampleBankInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddAudioFileFromFs(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", uintptr_t AudioApi_GetResourceDevAddr(u32 resourceId));

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_CreateStreamedSequence(AudioApiFileInfo* info));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_CreateStreamedBgm(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_CreateStreamedFanfare(AudioApiFileInfo* info, char* dir, char* filename));

#endif

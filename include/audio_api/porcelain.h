#ifndef __AUDIO_API_PORCELAIN_H__
#define __AUDIO_API_PORCELAIN_H__

/*! \file audio_api/porcelain.h
    \version 0.4.0
    \brief High level imports
 */

#include "types.h"

RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddResource(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", uintptr_t AudioApi_GetResourceDevAddr(u32 resourceId));

RECOMP_IMPORT("magemods_audio_api", bool AudioApi_AddAudioFile(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT("magemods_audio_api", uintptr_t AudioApi_GetAudioFileDevAddr(u32 resourceId, u32 trackNo));
RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_AddStreamedSequence(AudioApiFileInfo* info, char* dir, char* filename));

#endif

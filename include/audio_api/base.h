#ifndef __AUDIO_API_BASE_H__
#define __AUDIO_API_BASE_H__

/*! \file audio_api/base.h
    \version 0.5.0
    \brief Base level imports
 */

#include "types.h"

RECOMP_IMPORT("magemods_audio_api", uintptr_t AudioApi_AddDmaCallback(AudioApiDmaCallback callback, u32 arg0, u32 arg1, u32 arg2));

#endif

#ifndef __AUDIO_API_NATIVE__
#define __AUDIO_API_NATIVE__

#ifdef __cplusplus
#include <extlib/lib_recomp.hpp>
extern "C" {
#endif

typedef enum {                              // Note: specific implementation may vary by resource type
    RESOURCE_CACHE_NONE,                    // Do not cache
    RESOURCE_CACHE_PRELOAD,                 // Preload entire file
    RESOURCE_CACHE_PRELOAD_ON_USE,          // Preload upon DMA request
    RESOURCE_CACHE_PRELOAD_ON_USE_NO_EVICT, // Preload upon DMA request and never evict from cache
} ResourceCacheStrategy;

typedef struct DmaRequestArgs {
    u32 arg0;
    u32 arg1;
    u32 arg2;
} DmaRequestArgs;

typedef struct AudioFileInfo {
    u32 resourceId;
    u32 trackCount;
    u32 sampleRate;
    u32 sampleCount;
    u32 loopStart;
    u32 loopEnd;
    s32 loopCount;
} AudioFileInfo;


#ifdef MIPS
#include <recomp/modding.h>
RECOMP_IMPORT(".", bool AudioApiNative_Init(unsigned int log_level, unsigned char* mod_dir));
RECOMP_IMPORT(".", bool AudioApiNative_Ready());
RECOMP_IMPORT(".", bool AudioApiNative_Tick());
RECOMP_IMPORT(".", bool AudioApiNative_Dma(s16* buf, u32 size, u32 offset, DmaRequestArgs* args));
RECOMP_IMPORT(".", s32 AudioApiNative_AddAudioFile(AudioFileInfo* info, char* dir, char* filename,
                                                   ResourceCacheStrategy cacheStrategy));
#endif

#ifdef __cplusplus
}
#endif

#endif

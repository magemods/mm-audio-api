#ifndef __AUDIO_API_HEAP__
#define __AUDIO_API_HEAP__

#include "global.h"

#define IS_AUDIO_HEAP_MEMORY(x) ((U32(x) >= U32(gAudioHeap)) && (U32(x) < U32(gAudioHeap) + ARRAY_COUNT(gAudioHeap)))

extern u8 gAudioHeap[0x138000];

void* AudioHeap_LoadBufferAlloc(s32 tableType, s32 id, size_t size);
void AudioHeap_LoadBufferFree(s32 tableType, s32 id);
void* AudioApi_RspCacheAlloc(void* addr, size_t size, bool* didAllocate);
void* AudioApi_RspCacheMemcpy(void* addr, size_t size);

#endif

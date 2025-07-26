#ifndef __AUDIO_API_LOAD__
#define __AUDIO_API_LOAD__

#include "global.h"

#define DMA_CALLBACK_START_DEV_ADDR 0x10000000
#define IS_DMA_CALLBACK_DEV_ADDR(x) ((U32(x) >= DMA_CALLBACK_START_DEV_ADDR) && (U32(x) < K0BASE))

#define MAX_SAMPLE_DMA_PER_FRAME 0x100

extern OSIoMesg currAudioFrameDmaIoMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];
extern OSMesg currAudioFrameDmaMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];

typedef s32 (audioapi_dma_callback_t)(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1);

#endif

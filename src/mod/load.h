#ifndef __AUDIO_API_LOAD__
#define __AUDIO_API_LOAD__

#include "global.h"

#define MAX_SAMPLE_DMA_PER_FRAME 0x100

extern OSIoMesg currAudioFrameDmaIoMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];
extern OSMesg currAudioFrameDmaMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];

#endif

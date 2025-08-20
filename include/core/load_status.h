#ifndef __AUDIO_API_LOAD_STATUS__
#define __AUDIO_API_LOAD_STATUS__

#include <global.h>

typedef enum {
    LOAD_STATUS_WAITING,
    LOAD_STATUS_START,
    LOAD_STATUS_LOADING,
    LOAD_STATUS_DONE
} SlowLoadStatus;

extern u8* sExtSeqLoadStatus;
extern u8* sExtSoundFontLoadStatus;

s32 AudioApi_GetTableEntryLoadStatus(s32 tableType, s32 id);
void AudioApi_SetTableEntryLoadStatus(s32 tableType, s32 id, s32 status);
void AudioApi_PushFakeCache(s32 tableType, s32 cache, s32 id);

#endif

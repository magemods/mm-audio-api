#ifndef __AUDIO_API_LOAD__
#define __AUDIO_API_LOAD__

#include "global.h"

s32 AudioApi_AddTableEntry(AudioTable** tablePtr, AudioTableEntry* entry);
void AudioApi_ReplaceTableEntry(AudioTable* table, AudioTableEntry* entry, s32 id);
void AudioApi_RestoreTableEntry(AudioTable* table, s32 id);

#endif

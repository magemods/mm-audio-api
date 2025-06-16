#ifndef __AUDIO_API_LOAD__
#define __AUDIO_API_LOAD__

AudioTable* AudioApi_CopyTable(AudioTable* table);
s16 AudioApi_AddTableEntry(AudioTable** tablePtr, AudioTableEntry entry);
void AudioApi_ReplaceTableEntry(AudioTable* table, s16 id, AudioTableEntry entry);
void AudioApi_RestoreTableEntry(AudioTable* table, s16 id);

#endif

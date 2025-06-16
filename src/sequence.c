#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "load.h"

RECOMP_DECLARE_EVENT(AudioApi_onLoadSequence(s32 id, u8* ramAddr));

RECOMP_EXPORT s16 AudioApi_AddSequence(AudioTableEntry entry) {
    return AudioApi_AddTableEntry(&gAudioCtx.sequenceTable, entry);
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(s16 id, AudioTableEntry entry) {
    AudioApi_ReplaceTableEntry(gAudioCtx.sequenceTable, id, entry);
}

RECOMP_EXPORT void AudioApi_RestoreSequence(s16 id) {
    AudioApi_RestoreTableEntry(gAudioCtx.sequenceTable, id);
}

void AudioApi_LoadSequence(s32 id, u8* ramAddr) {
    if (id == 0) {
    }
    AudioApi_onLoadSequence(id, ramAddr);
}

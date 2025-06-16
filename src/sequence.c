#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "load.h"

RECOMP_DECLARE_EVENT(AudioApi_onLoadSequence(u8* ramAddr, s32 seqId));

RECOMP_EXPORT s32 AudioApi_AddSequence(AudioTableEntry entry) {
    return AudioApi_AddTableEntry(&gAudioCtx.sequenceTable, entry);
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(AudioTableEntry entry, s32 seqId) {
    AudioApi_ReplaceTableEntry(gAudioCtx.sequenceTable, entry, seqId);

}

RECOMP_EXPORT void AudioApi_RestoreSequence(s32 seqId) {
    AudioApi_RestoreTableEntry(gAudioCtx.sequenceTable, seqId);
}

void AudioApi_LoadSequence(u8* ramAddr, s32 seqId) {
    if (seqId == 0) {
    }
    AudioApi_onLoadSequence(ramAddr, seqId);
}

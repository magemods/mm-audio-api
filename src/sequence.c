#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "load.h"

RECOMP_DECLARE_EVENT(AudioApi_onLoadSequence(u8* ramAddr, s32 seqId));

RECOMP_EXPORT s32 AudioApi_AddSequence(AudioTableEntry* entry) {
    s32 seqId = AudioApi_AddTableEntry(&gAudioCtx.sequenceTable, entry);
    if (seqId != 0) {
        gAudioCtx.numSequences = gAudioCtx.sequenceTable->header.numEntries;
    }
    return seqId;
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(AudioTableEntry* entry, s32 seqId) {
    AudioApi_ReplaceTableEntry(gAudioCtx.sequenceTable, entry, seqId);
}

RECOMP_EXPORT void AudioApi_RestoreSequence(s32 seqId) {
    AudioApi_RestoreTableEntry(gAudioCtx.sequenceTable, seqId);
}

RECOMP_EXPORT void AudioApi_SetSequenceFontId(s32 seqId, s32 fontNum, s32 fontId) {
    s32 index;
    s32 numFonts;

    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) return;

    index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    numFonts = gAudioCtx.sequenceFontTable[index++];

    if (fontNum > numFonts) return;

    u8* seqFontEntry = &gAudioCtx.sequenceFontTable[index + fontNum];
    *seqFontEntry = fontId;
}

void AudioApi_LoadSequence(u8* ramAddr, s32 seqId) {
    if (seqId == 0) {
    }
    AudioApi_onLoadSequence(ramAddr, seqId);
}

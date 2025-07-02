#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "load.h"

extern u8 sSeqFlags[];
u8 sExtSeqFlags[0x100] = { 0 };

RECOMP_DECLARE_EVENT(AudioApi_onLoadSequence(u8* ramAddr, s32 seqId));

RECOMP_CALLBACK("*", recomp_on_init) void AudioApi_SequenceInit() {
    for (s32 i = 0; i < NA_BGM_MAX; i++) {
        sExtSeqFlags[i] = sSeqFlags[i];
    }
}

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

RECOMP_EXPORT void AudioApi_SetSequenceFlags(s32 seqId, u8 flags) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) return;

    sExtSeqFlags[seqId] = flags;
}

void AudioApi_LoadSequence(u8* ramAddr, s32 seqId) {
    if (seqId == 0) {
    }
    AudioApi_onLoadSequence(ramAddr, seqId);
}

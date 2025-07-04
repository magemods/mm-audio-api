#include "global.h"
#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"

#define MAX_FONTS_PER_SEQUENCE 4

extern u8 sSeqFlags[];
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate);

u8* sExtSeqFlags = sSeqFlags;
u16 sequenceTableCapacity = NA_BGM_MAX;

bool AudioApi_GrowSequenceTables();
bool AudioApi_RelocateSequenceFontTable();

RECOMP_DECLARE_EVENT(AudioApi_onLoadSequence(u8* ramAddr, s32 seqId));

RECOMP_CALLBACK(".", AudioApi_preInit) void AudioApi_SequenceInit() {
    if (!AudioApi_RelocateSequenceFontTable()) {
        recomp_printf("AudioApi: Error relocating sequence font table\n");
    }
}

RECOMP_EXPORT s32 AudioApi_AddSequence(AudioTableEntry* entry) {
    s32 seqId = gAudioCtx.sequenceTable->header.numEntries;
    if (seqId >= sequenceTableCapacity) {
        if (!AudioApi_GrowSequenceTables()) {
            return 0;
        }
    }
    gAudioCtx.sequenceTable->entries[seqId] = *entry;
    gAudioCtx.numSequences = ++gAudioCtx.sequenceTable->header.numEntries;
    return seqId;
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(s32 seqId, AudioTableEntry* entry) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return;
    }
    gAudioCtx.sequenceTable->entries[seqId] = *entry;
}

RECOMP_EXPORT void AudioApi_RestoreSequence(s32 seqId) {
    AudioTable* origTable = (AudioTable*)gSequenceTable;
    if (seqId >= origTable->header.numEntries) {
        return;
    }
    gAudioCtx.sequenceTable->entries[seqId] = origTable->entries[seqId];
}

RECOMP_EXPORT void AudioApi_AddSequenceFont(s32 seqId, s32 fontId) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return;
    }
    s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    u8* entry = &gAudioCtx.sequenceFontTable[index];
    u8 numFonts = entry[0];

    if (numFonts == MAX_FONTS_PER_SEQUENCE) {
        return;
    }
    numFonts = ++entry[0];
    entry[numFonts] = fontId;
}

RECOMP_EXPORT void AudioApi_ReplaceSequenceFont(s32 seqId, s32 fontNum, s32 fontId) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries || fontNum >= MAX_FONTS_PER_SEQUENCE) {
        return;
    }
    s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    u8* entry = &gAudioCtx.sequenceFontTable[index];
    u8 numFonts = entry[0];

    if (fontNum >= numFonts) {
        return;
    }
    entry[fontNum + 1] = fontId;
}

RECOMP_EXPORT void AudioApi_SetSequenceFlags(s32 seqId, u8 flags) {
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) return;

    sExtSeqFlags[seqId] = flags;
}

RECOMP_CALLBACK(".", AudioApi_afterSyncDma) void AudioApi_DispatchSequenceEvent(uintptr_t devAddr, u8* ramAddr) {
    AudioTableEntry* entry;
    s32 seqId;

    for (seqId = 0; seqId < gAudioCtx.sequenceTable->header.numEntries; seqId++) {
        entry = &gAudioCtx.sequenceTable->entries[seqId];
        if (entry->romAddr == devAddr) {
            AudioApi_onLoadSequence(ramAddr, seqId);
            return;
        }
    }
}

bool AudioApi_GrowSequenceTables() {
    u16 newCapacity = sequenceTableCapacity << 1;
    size_t oldSize, newSize;
    AudioTable* newSeqTable = NULL;
    u8* newSeqFontTable = NULL;
    u8* newSeqFlags = NULL;

    // Grow gAudioCtx.sequenceTable
    oldSize = sizeof(AudioTableHeader) + sequenceTableCapacity * sizeof(AudioTableEntry);
    newSize = sizeof(AudioTableHeader) + newCapacity * sizeof(AudioTableEntry);
    newSeqTable = recomp_alloc(newSize);
    if (!newSeqTable) {
        goto cleanup;
    }
    Lib_MemSet(newSeqTable, 0, newSize);
    Lib_MemCpy(newSeqTable, gAudioCtx.sequenceTable, oldSize);

    // Grow gAudioCtx.sequenceFontTable
    oldSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * sequenceTableCapacity;
    newSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * newCapacity;
    newSeqFontTable = recomp_alloc(newSize);
    if (!newSeqFontTable) {
        goto cleanup;
    }
    Lib_MemSet(newSeqFontTable, 0, newSize);
    Lib_MemCpy(newSeqFontTable, gAudioCtx.sequenceFontTable, oldSize);

    // Grow sExtSeqFlags
    oldSize = sizeof(u8) * sequenceTableCapacity;
    newSize = sizeof(u8) * newCapacity;
    newSeqFlags = recomp_alloc(newSize);
    if (!newSeqFlags) {
        goto cleanup;
    }
    Lib_MemSet(newSeqFlags, 0, newSize);
    Lib_MemCpy(newSeqFlags, sExtSeqFlags, oldSize);

    // Free old tables
    if (!IS_KSEG0(gAudioCtx.sequenceTable)) recomp_free(gAudioCtx.sequenceTable);
    if (!IS_KSEG0(gAudioCtx.sequenceFontTable)) recomp_free(gAudioCtx.sequenceFontTable);
    if (!IS_KSEG0(sExtSeqFlags)) recomp_free(sExtSeqFlags);

    // Store new tables
    gAudioCtx.sequenceTable = newSeqTable;
    gAudioCtx.sequenceFontTable = newSeqFontTable;
    sExtSeqFlags = newSeqFlags;
    return true;

 cleanup:
    if (newSeqTable != NULL) {
        recomp_free(newSeqTable);
    }
    if (newSeqFontTable != NULL) {
        recomp_free(newSeqFontTable);
    }
    if (newSeqFlags != NULL) {
        recomp_free(newSeqFlags);
    }
    return false;
}

bool AudioApi_RelocateSequenceFontTable() {
    // The sequence font table is a bit strange.
    // You're supposed to cast it to an array of u16 and read the entry at the specified seqId.
    // That gives you the offset from the start of the table when reading it as an array of u8.
    // This is done because each sequence can have a variable number of fonts, although in the
    // vanilla ROM only sequence 0 has two fonts, while the rest have one.
    // We want to support more fonts per sequence, but we don't want to be constantly resizing
    // the table each time. We will resize it so each sequence can support up to four fonts.

    size_t newSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * sequenceTableCapacity;
    u8* newSeqFontTable = recomp_alloc(newSize);

    if (!newSeqFontTable) {
        return false;
    }
    Lib_MemSet(newSeqFontTable, 0, newSize);

    u16* header = (u16*)newSeqFontTable;
    u8* entries = newSeqFontTable + sizeof(u16) * sequenceTableCapacity;

    for (u16 seqId = 0; seqId < NA_BGM_MAX; seqId++) {
        // Write the offset into the header
        header[seqId] = ((uintptr_t) entries - (uintptr_t) header) + (seqId * (MAX_FONTS_PER_SEQUENCE + 1));
        //header[seqId] = (sizeof(u16) * sequenceTableCapacity) + (seqId * (MAX_FONTS_PER_SEQUENCE + 1));

        // Find the entry in the old table and read the number of fonts
        s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
        u8* entry = &gAudioCtx.sequenceFontTable[index];
        u8 numFonts = entry[0];

        // Copy old entry into new table
        Lib_MemCpy(entries + seqId * (MAX_FONTS_PER_SEQUENCE + 1), entry, numFonts + 1);
    }

    gAudioCtx.sequenceFontTable = newSeqFontTable;
    return true;
}

// gAudioCtx.seqLoadStatus has 128 max entries, so we can't read/write anything higher
// The sequence should load immediately since it's already in RAM, but we can also
// make a new array to track the load status of custom sequences.

RECOMP_PATCH u8* AudioLoad_SyncLoadSeq(s32 seqId) {
    s32 pad;
    s32 didAllocate;

    if (seqId <= 0x7F) {
        if (gAudioCtx.seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] == LOAD_STATUS_IN_PROGRESS) {
            return NULL;
        }
    }

    return AudioLoad_SyncLoad(SEQUENCE_TABLE, seqId, &didAllocate);
}

RECOMP_PATCH void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 loadStatus) {
    if (seqId > 0x7F) {
        return;
    }
    if ((seqId != 0xFF) && (gAudioCtx.seqLoadStatus[seqId] != LOAD_STATUS_PERMANENT)) {
        gAudioCtx.seqLoadStatus[seqId] = loadStatus;
    }
}

RECOMP_PATCH s32 AudioLoad_IsSeqLoadComplete(s32 seqId) {
    if (seqId > 0x7F) {
        return true;
    } else if (gAudioCtx.seqLoadStatus[seqId] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else if (gAudioCtx.seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else {
        return false;
    }
}

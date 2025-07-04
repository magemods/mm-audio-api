#include "global.h"
#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "queue.h"
#include "util.h"

#define MAX_FONTS_PER_SEQUENCE 4

extern u8 sSeqFlags[];
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate);

AudioApiQueue* sequenceQueue;
u16 sequenceTableCapacity = NA_BGM_MAX;
u8* sExtSeqFlags = sSeqFlags;
u8* sExtSeqLoadStatus = gAudioCtx.seqLoadStatus;

void AudioApi_SequenceQueueDrain(AudioApiCmd* cmd);
bool AudioApi_GrowSequenceTables();
bool AudioApi_RelocateSequenceFontTable();

RECOMP_DECLARE_EVENT(AudioApi_SequenceLoaded(s32 seqId, u8* ramAddr));

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_SequenceInit() {
    sequenceQueue = AudioApi_QueueCreate();
    if (!AudioApi_RelocateSequenceFontTable()) {
        recomp_printf("AudioApi: Error relocating sequence font table\n");
    }
}

RECOMP_CALLBACK(".", AudioApi_ReadyInternal) void AudioApi_SequenceReady() {
    AudioApi_QueueDrain(sequenceQueue, AudioApi_SequenceQueueDrain);
}

RECOMP_EXPORT s32 AudioApi_AddSequence(AudioTableEntry* entry) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    s32 newSeqId = gAudioCtx.sequenceTable->header.numEntries;
    if ((newSeqId & 0xFF) == 0xFF) {
        newSeqId++;
    }
    if (newSeqId >= sequenceTableCapacity) {
        if (!AudioApi_GrowSequenceTables()) {
            return -1;
        }
    }
    gAudioCtx.sequenceTable->entries[newSeqId] = *entry;
    gAudioCtx.numSequences = ++gAudioCtx.sequenceTable->header.numEntries;
    return newSeqId;
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(s32 seqId, AudioTableEntry* entry) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioTableEntry* copy = recomp_alloc(sizeof(AudioTableEntry));
        if (!copy) {
            return;
        }
        *copy = *entry;
        AudioApi_QueueCmd(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE, seqId, 0, (void**)&copy);
        return;
    }
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return;
    }
    gAudioCtx.sequenceTable->entries[seqId] = *entry;
}

RECOMP_EXPORT void AudioApi_RestoreSequence(s32 seqId) {
    if (gAudioApiInitPhase < AUDIOAPI_INIT_READY) {
        return;
    }
    AudioTable* origTable = (AudioTable*)gSequenceTable;
    if (seqId >= origTable->header.numEntries) {
        return;
    }
    gAudioCtx.sequenceTable->entries[seqId] = origTable->entries[seqId];
}

RECOMP_EXPORT s32 AudioApi_AddSequenceFont(s32 seqId, s32 fontId) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return -1;
    }
    s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    u8* entry = &gAudioCtx.sequenceFontTable[index];
    u8 numFonts = entry[0];

    if (numFonts == MAX_FONTS_PER_SEQUENCE) {
        return -1;
    }
    entry[0]++;
    entry[numFonts + 1] = fontId;
    return numFonts + 1;
}

RECOMP_EXPORT void AudioApi_ReplaceSequenceFont(s32 seqId, s32 fontNum, s32 fontId) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioApi_QueueCmd(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT, seqId, fontNum, (void**)&fontId);
        return;
    }
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

RECOMP_EXPORT void AudioApi_RestoreSequenceFont(s32 seqId, s32 fontNum) {
    if (gAudioApiInitPhase < AUDIOAPI_INIT_READY) {
        return;
    }
    if (seqId >= NA_BGM_MAX) {
        return;
    }
    s32 index;
    u8* entry;
    u8 numFonts;
    u8 origFontId;

    index = ((u16*)gSequenceFontTable)[seqId];
    entry = &gSequenceFontTable[index];
    numFonts = entry[0];

    if (fontNum >= numFonts) {
        return;
    }
    origFontId = entry[fontNum + 1];

    index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    entry = &gAudioCtx.sequenceFontTable[index];
    entry[fontNum + 1] = origFontId;
}

RECOMP_EXPORT void AudioApi_SetSequenceFlags(s32 seqId, u8 flags) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioApi_QueueCmd(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT, seqId, 0, (void**)&flags);
        return;
    }
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries) {
        return;
    }
    sExtSeqFlags[seqId] = flags;
}

RECOMP_EXPORT void AudioApi_RestoreSequenceFlags(s32 seqId) {
    if (gAudioApiInitPhase < AUDIOAPI_INIT_READY) {
        return;
    }
    if (seqId >= NA_BGM_MAX) {
        return;
    }
    sExtSeqFlags[seqId] = sSeqFlags[seqId];
}

RECOMP_CALLBACK(".", AudioApi_AfterSyncDma) void AudioApi_SequenceAfterSyncDma(uintptr_t devAddr, u8* ramAddr) {
    for (s32 seqId = 0; seqId < gAudioCtx.sequenceTable->header.numEntries; seqId++) {
        AudioTableEntry* entry = &gAudioCtx.sequenceTable->entries[seqId];
        if (entry->romAddr == devAddr) {
            AudioApi_SequenceLoaded(seqId, ramAddr);
            return;
        }
    }
}

void AudioApi_SequenceQueueDrain(AudioApiCmd* cmd) {
    switch (cmd->op) {
    case AUDIOAPI_CMD_OP_REPLACE_SEQUENCE:
        AudioApi_ReplaceSequence(cmd->arg0, (AudioTableEntry*) cmd->asPtr);
        recomp_free(cmd->asPtr);
        break;
    case AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT:
        AudioApi_ReplaceSequenceFont(cmd->arg0, cmd->arg1, cmd->asInt);
        break;
    case AUDIOAPI_CMD_OP_SET_SEQUENCE_FLAGS:
        AudioApi_SetSequenceFlags(cmd->arg0, cmd->asUbyte);
        break;
    default:
        break;
    }
}

bool AudioApi_GrowSequenceTables() {
    u16 oldCapacity = sequenceTableCapacity;
    u16 newCapacity = sequenceTableCapacity << 1;
    size_t oldSize, newSize;
    AudioTable* newSeqTable = NULL;
    u8* newSeqFontTable = NULL;
    u8* newSeqFlags = NULL;
    u8* newSeqLoadStatus = NULL;

    // Grow gAudioCtx.sequenceTable
    oldSize = sizeof(AudioTableHeader) + oldCapacity * sizeof(AudioTableEntry);
    newSize = sizeof(AudioTableHeader) + newCapacity * sizeof(AudioTableEntry);
    newSeqTable = recomp_alloc(newSize);
    if (!newSeqTable) {
        goto cleanup;
    }
    Lib_MemSet(newSeqTable, 0, newSize);
    Lib_MemCpy(newSeqTable, gAudioCtx.sequenceTable, oldSize);

    // Grow gAudioCtx.sequenceFontTable
    oldSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * oldCapacity;
    newSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * newCapacity;
    newSeqFontTable = recomp_alloc(newSize);
    if (!newSeqFontTable) {
        goto cleanup;
    }
    Lib_MemSet(newSeqFontTable, 0, newSize);
    Lib_MemCpy(newSeqFontTable, gAudioCtx.sequenceFontTable, oldSize);

    // Grow sExtSeqFlags
    oldSize = sizeof(u8) * oldCapacity;
    newSize = sizeof(u8) * newCapacity;
    newSeqFlags = recomp_alloc(newSize);
    if (!newSeqFlags) {
        goto cleanup;
    }
    Lib_MemSet(newSeqFlags, 0, newSize);
    Lib_MemCpy(newSeqFlags, sExtSeqFlags, oldSize);

    // Grow sExtSeqLoadStatus
    oldSize = sizeof(u8) * oldCapacity;
    newSize = sizeof(u8) * newCapacity;
    newSeqLoadStatus = recomp_alloc(newSize);
    if (!newSeqLoadStatus) {
        goto cleanup;
    }
    Lib_MemSet(newSeqLoadStatus, 0, newSize);
    Lib_MemCpy(newSeqLoadStatus, sExtSeqLoadStatus, oldSize);

    // Free old tables
    if (IS_MOD_MEMORY(gAudioCtx.sequenceTable)) recomp_free(gAudioCtx.sequenceTable);
    if (IS_MOD_MEMORY(gAudioCtx.sequenceFontTable)) recomp_free(gAudioCtx.sequenceFontTable);
    if (IS_MOD_MEMORY(sExtSeqFlags)) recomp_free(sExtSeqFlags);
    if (IS_MOD_MEMORY(sExtSeqLoadStatus)) recomp_free(sExtSeqLoadStatus);

    // Store new tables
    gAudioCtx.sequenceTable = newSeqTable;
    gAudioCtx.sequenceFontTable = newSeqFontTable;
    sExtSeqFlags = newSeqFlags;
    sExtSeqLoadStatus = newSeqLoadStatus;
    sequenceTableCapacity = newCapacity;
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
    if (newSeqLoadStatus != NULL) {
        recomp_free(newSeqLoadStatus);
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

// gAudioCtx.seqLoadStatus has 128 max entries, so anything higher will read/write from our own array

RECOMP_PATCH u8* AudioLoad_SyncLoadSeq(s32 seqId) {
    u8* seqLoadStatus = seqId < 0x80 ? gAudioCtx.seqLoadStatus : sExtSeqLoadStatus;
    s32 pad;
    s32 didAllocate;

    if (seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] == LOAD_STATUS_IN_PROGRESS) {
        return NULL;
    }

    return AudioLoad_SyncLoad(SEQUENCE_TABLE, seqId, &didAllocate);
}

RECOMP_PATCH void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 loadStatus) {
    u8* seqLoadStatus = seqId < 0x80 ? gAudioCtx.seqLoadStatus : sExtSeqLoadStatus;
    if ((seqId != 0xFF) && (seqLoadStatus[seqId] != LOAD_STATUS_PERMANENT)) {
        seqLoadStatus[seqId] = loadStatus;
    }
}

RECOMP_PATCH s32 AudioLoad_IsSeqLoadComplete(s32 seqId) {
    u8* seqLoadStatus = seqId < 0x80 ? gAudioCtx.seqLoadStatus : sExtSeqLoadStatus;
    if (seqId == 0xFF) {
        return true;
    } else if (seqLoadStatus[seqId] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else if (seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else {
        return false;
    }
}

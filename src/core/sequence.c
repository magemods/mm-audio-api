#include <global.h>
#include <recomp/modding.h>
#include <recomp/recomputils.h>
#include <utils/misc.h>
#include <utils/queue.h>
#include <core/heap.h>
#include <core/init.h>
#include <core/load_status.h>
#include <core/sequence_functions.h>

/**
 * This file provides the public API for modifying sequences in the various sequence tables
 *
 * There are two main tables for sequence data: `gSequenceTable` and `gSequenceFontTable`.
 *
 * The first table contains the ROM address, medium, size, and cache policy. For custom sequences,
 * we set the romAddr field to the location in mod memory where the sequence is.
 *
 * The second table associates sequences with the fonts they require. Sequence 0 is the only entry
 * that contains more than one font, but this mod expands the table to allow up to four fonts each.
 *
 * There are also two arrays, `sSeqFlags` and `sSeqLoadStatus` which are expanded to allow more than
 * the hardcoded length of the array.
 *
 * The API will need to allow some way to also modify sequence data, but for now such changes must
 * be done using the `AudioApi_SequenceLoaded()` event, if necessary.
 */

#define MAX_FONTS_PER_SEQUENCE 4

extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate);

typedef enum {
    AUDIOAPI_CMD_OP_REPLACE_SEQUENCE,
    AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT,
    AUDIOAPI_CMD_OP_SET_SEQUENCE_FLAGS,
} AudioApiSequenceQueueOp;

RecompQueue* sequenceQueue;
u16 sequenceTableCapacity = NA_BGM_MAX;

void AudioApi_SequenceQueueDrain(RecompQueueCmd* cmd);
bool AudioApi_GrowSequenceTables();
u8* AudioApi_RebuildSequenceFontTable(u16 oldCapacity, u16 newCapacity);

RECOMP_DECLARE_EVENT(AudioApi_SequenceLoaded(s32 seqId, u8* ramAddr));

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_SequenceInit() {
    sequenceQueue = RecompQueue_Create();

    u8* newSeqFontTable = AudioApi_RebuildSequenceFontTable(sequenceTableCapacity, sequenceTableCapacity);
    if (newSeqFontTable != NULL) {
        gAudioCtx.sequenceFontTable = newSeqFontTable;
    } else {
        recomp_printf("AudioApi: Error rebuilding sequence font table\n");
    }

    // Debugging, make new sequences start at 256
    AudioApi_GrowSequenceTables();
    gAudioCtx.sequenceTable->header.numEntries = gAudioCtx.numSequences = sequenceTableCapacity;
}

RECOMP_CALLBACK(".", AudioApi_ReadyInternal) void AudioApi_SequenceReady() {
    RecompQueue_Drain(sequenceQueue, AudioApi_SequenceQueueDrain);
    RecompQueue_Destroy(sequenceQueue);
}

RECOMP_EXPORT s32 AudioApi_AddSequence(AudioTableEntry* entry) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    // Find the next available sequence ID. It should not end in 0xFE or 0xFF since
    // that may be misinterpreted by some vanilla functions as disabled or unknown.
    s32 newSeqId = gAudioCtx.sequenceTable->header.numEntries;
    while ((newSeqId & 0xFF) == (NA_BGM_DISABLED & 0xFF) || (newSeqId & 0xFF) == (NA_BGM_UNKNOWN & 0xFF)) {
        newSeqId++;
    }
    if (newSeqId >= sequenceTableCapacity) {
        if (!AudioApi_GrowSequenceTables()) {
            return -1;
        }
    }
    gAudioCtx.sequenceTable->entries[newSeqId] = *entry;
    gAudioCtx.numSequences = gAudioCtx.sequenceTable->header.numEntries = newSeqId + 1;
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
        RecompQueue_PushIfNotQueued(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE,
                                     seqId, 0, (void**)&copy);
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

    for (s32 i = MAX_FONTS_PER_SEQUENCE; i > 1; i--) {
        entry[i] = entry[i - 1];
    }
    entry[1] = fontId;
    entry[0]++;

    return numFonts + 1;
}

RECOMP_EXPORT void AudioApi_ReplaceSequenceFont(s32 seqId, s32 fontNum, s32 fontId) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        RecompQueue_PushIfNotQueued(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT,
                                     seqId, fontNum, (void**)&fontId);
        return;
    }
    if (seqId >= gAudioCtx.sequenceTable->header.numEntries || fontNum >= MAX_FONTS_PER_SEQUENCE) {
        return;
    }
    s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
    u8* entry = &gAudioCtx.sequenceFontTable[index];
    u8 numFonts = entry[0];

    if (fontNum >= numFonts || fontNum < 0) {
        return;
    }
    entry[numFonts - fontNum] = fontId;
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

RECOMP_EXPORT u8 AudioApi_GetSequenceFlags(s32 seqId) {
    return AudioApi_GetSequenceFlagsInternal(seqId);
}

RECOMP_EXPORT void AudioApi_SetSequenceFlags(s32 seqId, u8 flags) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        RecompQueue_PushIfNotQueued(sequenceQueue, AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT,
                                     seqId, 0, (void**)&flags);
        return;
    }
    AudioApi_SetSequenceFlagsInternal(seqId, flags);
}

RECOMP_EXPORT void AudioApi_RestoreSequenceFlags(s32 seqId) {
    if (gAudioApiInitPhase < AUDIOAPI_INIT_READY) {
        return;
    }
    if (seqId >= NA_BGM_MAX) {
        return;
    }
    AudioApi_SetSequenceFlagsInternal(seqId, sSeqFlags[seqId]);
}

void AudioApi_SequenceQueueDrain(RecompQueueCmd* cmd) {
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

RECOMP_CALLBACK(".", AudioApi_SequenceLoadedInternal) void AudioApi_RelocateSequence(s32 seqId, void** ramAddrPtr) {
    // If this sequence was just loaded into the audio heap load buffer, we need to relocate it
    // to mod memory now.
    if (IS_AUDIO_HEAP_MEMORY(*ramAddrPtr)) {
        size_t size = gAudioCtx.sequenceTable->entries[seqId].size;
        void* ramAddr = recomp_alloc(size);
        Lib_MemCpy(ramAddr, *ramAddrPtr, size);
        AudioHeap_LoadBufferFree(SEQUENCE_TABLE, seqId);
        *ramAddrPtr = ramAddr;
    }

    // If this sequence was loaded from ROM or a callback, update the entry's romAddr to our new
    // permanent memory.
    if (!IS_KSEG0(gAudioCtx.sequenceTable->entries[seqId].romAddr)) {
        gAudioCtx.sequenceTable->entries[seqId].romAddr = (uintptr_t)(*ramAddrPtr);
    }

    // Dispatch loaded event
    AudioApi_SequenceLoaded(seqId, (u8*)(*ramAddrPtr));
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
    newSeqFontTable = AudioApi_RebuildSequenceFontTable(oldCapacity, newCapacity);
    if (newSeqFontTable == NULL) {
        goto cleanup;
    }

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
    if (IS_RECOMP_ALLOC(gAudioCtx.sequenceTable)) recomp_free(gAudioCtx.sequenceTable);
    if (IS_RECOMP_ALLOC(gAudioCtx.sequenceFontTable)) recomp_free(gAudioCtx.sequenceFontTable);
    if (IS_RECOMP_ALLOC(sExtSeqFlags)) recomp_free(sExtSeqFlags);
    if (IS_RECOMP_ALLOC(sExtSeqLoadStatus)) recomp_free(sExtSeqLoadStatus);

    // Store new tables
    recomp_printf("AudioApi: Resized sequences tables to %d\n", newCapacity);
    gAudioCtx.sequenceTable = newSeqTable;
    gAudioCtx.sequenceFontTable = newSeqFontTable;
    sExtSeqFlags = newSeqFlags;
    sExtSeqLoadStatus = newSeqLoadStatus;
    sequenceTableCapacity = newCapacity;
    return true;

 cleanup:
    recomp_printf("AudioApi: Error resizing sequences tables to %d\n", newCapacity);
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

u8* AudioApi_RebuildSequenceFontTable(u16 oldCapacity, u16 newCapacity) {
    // The sequence font table is a bit strange.
    // You're supposed to cast it to an array of u16 and read the entry at the specified seqId.
    // That gives you the offset from the start of the table when reading it as an array of u8.
    // This is done because each sequence can have a variable number of fonts, although in the
    // vanilla ROM only sequence 0 has two fonts, while the rest have one.
    // We want to support more fonts per sequence, but we don't want to be constantly resizing
    // the table each time, so we will resize it so each sequence can support up to four fonts.

    size_t newSize = (sizeof(u16) + MAX_FONTS_PER_SEQUENCE + 1) * newCapacity;
    u8* newSeqFontTable = recomp_alloc(newSize);

    if (!newSeqFontTable) {
        return NULL;
    }
    Lib_MemSet(newSeqFontTable, 0, newSize);

    u16* header = (u16*)newSeqFontTable;
    u8* entries = newSeqFontTable + sizeof(u16) * newCapacity;

    for (u16 seqId = 0; seqId < newCapacity; seqId++) {
        // Write the offset into the header
        header[seqId] = (sizeof(u16) * newCapacity) + (seqId * (MAX_FONTS_PER_SEQUENCE + 1));

        // Find the entry in the old table and read the number of fonts
        if (seqId < oldCapacity) {
            s32 index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
            u8* entry = &gAudioCtx.sequenceFontTable[index];
            u8 numFonts = entry[0];

            // Copy old entry into new table
            Lib_MemCpy(entries + seqId * (MAX_FONTS_PER_SEQUENCE + 1), entry, numFonts + 1);
        }
    }

    return newSeqFontTable;
}

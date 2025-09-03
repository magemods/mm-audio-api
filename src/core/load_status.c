#include <core/load_status.h>
#include <recomp/modding.h>
#include <recomp/recompdata.h>
#include <recomp/recomputils.h>

/**
 * This file is responsible for keeping track of fake load statuses and cache entries.
 *
 * Once a sequence or soundfont is loaded from the ROM, or if was already in RAM from a mod, we
 * could just treat the resource as permanently loaded. However the game does not expect this and
 * it actually breaks things. Additionally, the load status arrays are hardcoded in length so we
 * also need an extended array to keep track of new sequences and soundfonts.
 *
 * The permanent cache keeps track of whether the game asked for the resource to be permanently
 * loaded. These entries would normally not be flushed when the audio heap is reset. In vanilla,
 * this is sequence 0, and soundfonts 0 / 1.
 *
 * The persistent cache keeps track of whether the game asked the resources to be persistently
 * loaded. These entries are only flushed when explicitely asked to be, or when the audio heap is
 * reset. This is opposed to the temporary cache which has limited entries which are automatically
 * evicted when necessary. The persistent cache is the most important to mock in the same way the
 * vanilla game does since sequences may be stopped by popping entries from this cache.
 *
 * The loaded cache keeps track of if we've ever loaded this resource. This is so that we don't call
 * functions that modify the resource more than once on the stored copy in RAM.
 */

#define MAX_PERSISTENT_CACHE_ENTRIES 16

typedef struct PersistentCacheEntry {
    s16 tableType;
    u32 id;
} PersistentCacheEntry;

typedef struct PersistentCache {
    s16 numEntries;
    PersistentCacheEntry entries[MAX_PERSISTENT_CACHE_ENTRIES];
} PersistentCache;

PersistentCache persistentCache;
U32HashsetHandle permanentCache;
U32HashsetHandle loadedCache;

u8* sExtSeqLoadStatus = gAudioCtx.seqLoadStatus;
u8* sExtSoundFontLoadStatus = gAudioCtx.fontLoadStatus;

extern AudioTable* AudioLoad_GetLoadTable(s32 tableType);
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_LoadStatusInit() {
    permanentCache = recomputil_create_u32_hashset();
    loadedCache = recomputil_create_u32_hashset();
}

// ======== LOAD STATUS FUNCTIONS ========

s32 AudioApi_GetTableEntryLoadStatus(s32 tableType, s32 id) {
    if ((id & 0xFF) == 0xFF || (id & 0xFF) == 0xFE) return LOAD_STATUS_PERMANENT;

    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    if (tableType == SEQUENCE_TABLE) {
        return sExtSeqLoadStatus[realId];
    } else if (tableType == FONT_TABLE) {
        return sExtSoundFontLoadStatus[realId];
    } else {
        return LOAD_STATUS_NOT_LOADED;
    }
}

void AudioApi_SetTableEntryLoadStatus(s32 tableType, s32 id, s32 status) {
    if ((id & 0xFF) == 0xFF || (id & 0xFF) == 0xFE) return;

    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    if (tableType == SEQUENCE_TABLE) {
        sExtSeqLoadStatus[realId] = status;
    } else if (tableType == FONT_TABLE) {
        sExtSoundFontLoadStatus[realId] = status;
    }
}

RECOMP_PATCH void AudioHeap_ResetLoadStatus(void) {
    s32 i;

    persistentCache.numEntries = 0;

    for (i = 0; i < gAudioCtx.soundFontTable->header.numEntries; i++) {
        if (AudioApi_GetTableEntryLoadStatus(FONT_TABLE, i) != LOAD_STATUS_PERMANENT) {
            AudioApi_SetTableEntryLoadStatus(FONT_TABLE, i, LOAD_STATUS_NOT_LOADED);
        }
    }
    for (i = 0; i < gAudioCtx.sequenceTable->header.numEntries; i++) {
        if (AudioApi_GetTableEntryLoadStatus(SEQUENCE_TABLE, i) != LOAD_STATUS_PERMANENT) {
            AudioApi_SetTableEntryLoadStatus(SEQUENCE_TABLE, i, LOAD_STATUS_NOT_LOADED);
        }
    }
}

RECOMP_PATCH s32 AudioLoad_IsSeqLoadComplete(s32 seqId) {
    return AudioApi_GetTableEntryLoadStatus(SEQUENCE_TABLE, seqId) >= LOAD_STATUS_COMPLETE;
}

RECOMP_PATCH void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 status) {
    AudioApi_SetTableEntryLoadStatus(SEQUENCE_TABLE, seqId, status);
}

RECOMP_PATCH s32 AudioLoad_IsFontLoadComplete(s32 fontId) {
    return AudioApi_GetTableEntryLoadStatus(FONT_TABLE, fontId) >= LOAD_STATUS_COMPLETE;
}

RECOMP_PATCH void AudioLoad_SetFontLoadStatus(s32 fontId, s32 status) {
    AudioApi_SetTableEntryLoadStatus(FONT_TABLE, fontId, status);
}

// ======== FAKE CACHE FUNCTIONS ========

RECOMP_PATCH void* AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id) {
    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    u32 key = tableType << 24 | realId;

    if (!recomputil_u32_hashset_contains(permanentCache, key) && cache == CACHE_PERMANENT) {
        return NULL;
    }
    if (!recomputil_u32_hashset_contains(loadedCache, key)) {
        return NULL;
    }
    if (!IS_KSEG0(table->entries[realId].romAddr)) {
        return NULL;
    }
    return (void*)table->entries[realId].romAddr;
}

RECOMP_PATCH void* AudioHeap_SearchRegularCaches(s32 tableType, s32 cache, s32 id) {
    return AudioHeap_SearchCaches(tableType, CACHE_EITHER, id);
}

RECOMP_PATCH void* AudioHeap_SearchPermanentCache(s32 tableType, s32 id) {
    return AudioHeap_SearchCaches(tableType, CACHE_PERMANENT, id);
}

void AudioApi_PushFakeCache(s32 tableType, s32 cachePolicy, s32 id) {
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    u32 key = tableType << 24 | realId;
    PersistentCacheEntry* entry;
    s32 i;

    // Always push into loaded cache, this lets us only initialize an entry once
    recomputil_u32_hashset_insert(loadedCache, key);

    if (cachePolicy == CACHE_LOAD_PERMANENT) {
        recomputil_u32_hashset_insert(permanentCache, key);
    }

    if (cachePolicy == CACHE_LOAD_PERSISTENT && persistentCache.numEntries < MAX_PERSISTENT_CACHE_ENTRIES) {
        for (i = 0; i < persistentCache.numEntries; i++) {
            entry = &persistentCache.entries[i];
            if (entry->tableType == tableType && entry->id == realId) {
                return;
            }
        }

        entry = &persistentCache.entries[persistentCache.numEntries++];
        entry->tableType = tableType;
        entry->id = realId;
    }
}

RECOMP_PATCH void AudioHeap_PopPersistentCache(s32 tableType) {
    PersistentCacheEntry* entry = NULL;
    s32 i;

    // Find the most recent entry of tableType, or return if not found
    for (i = persistentCache.numEntries - 1; i >= 0; i--) {
        if (persistentCache.entries[i].tableType == tableType) {
            entry = &persistentCache.entries[i];
            break;
        }
    }

    if (entry == NULL) {
        return;
    }

    // Discard entry and set load status
    if (tableType == FONT_TABLE) {
        AudioHeap_DiscardFont(entry->id);
    }

    AudioApi_SetTableEntryLoadStatus(tableType, entry->id, LOAD_STATUS_NOT_LOADED);

    // Decrement numEntries and shift
    for (persistentCache.numEntries--; i < persistentCache.numEntries; i++) {
        persistentCache.entries[i] = persistentCache.entries[i + 1];
    }
}

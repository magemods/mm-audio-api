#include "global.h"
#include "modding.h"

/**
 * The purpose of this file is to patch just a few functions found in heap.c in order to
 * support more than 256 sequences. This file will also likely be responsible for handling
 * unlimited soundfont entries as well.
 */

void AudioHeap_DiscardSampleBank(s32 sampleBankId);
void AudioHeap_DiscardSampleBanks(void);

RECOMP_IMPORT(".", s32 AudioApi_GetSeqPlayerSeqId(SequencePlayer* seqPlayer));

RECOMP_PATCH void AudioHeap_DiscardSequence(s32 seqId) {
    s32 i;

    // @mod Use our seqId getter function
    for (i = 0; i < gAudioCtx.audioBufferParameters.numSequencePlayers; i++) {
        if (gAudioCtx.seqPlayers[i].enabled && AudioApi_GetSeqPlayerSeqId(&gAudioCtx.seqPlayers[i]) == seqId) {
            AudioScript_SequencePlayerDisable(&gAudioCtx.seqPlayers[i]);
        }
    }
}

RECOMP_PATCH void* AudioHeap_AllocCached(s32 tableType, size_t size, s32 cache, s32 id) {
    AudioCache* loadedCache;
    AudioTemporaryCache* temporaryCache;
    AudioAllocPool* temporaryPool;
    void* persistentAddr;
    void* temporaryAddr;
    u8 loadStatusEntry0;
    u8 loadStatusEntry1;
    s32 i;
    u8* loadStatus;
    s32 side;

    switch (tableType) {
        case SEQUENCE_TABLE:
            loadedCache = &gAudioCtx.seqCache;
            loadStatus = gAudioCtx.seqLoadStatus;
            break;

        case FONT_TABLE:
            loadedCache = &gAudioCtx.fontCache;
            loadStatus = gAudioCtx.fontLoadStatus;
            break;

        case SAMPLE_TABLE:
            loadedCache = &gAudioCtx.sampleBankCache;
            loadStatus = gAudioCtx.sampleFontLoadStatus;
            break;
    }

    if (cache == CACHE_TEMPORARY) {
        temporaryCache = &loadedCache->temporary;
        temporaryPool = &temporaryCache->pool;

        if ((s32)temporaryPool->size < (s32)size) {
            return NULL;
        }

        loadStatusEntry0 =
            (temporaryCache->entries[0].id == -1) ? LOAD_STATUS_NOT_LOADED : loadStatus[temporaryCache->entries[0].id];
        loadStatusEntry1 =
            (temporaryCache->entries[1].id == -1) ? LOAD_STATUS_NOT_LOADED : loadStatus[temporaryCache->entries[1].id];

        if (tableType == FONT_TABLE) {
            if (loadStatusEntry0 == LOAD_STATUS_MAYBE_DISCARDABLE) {
                for (i = 0; i < gAudioCtx.numNotes; i++) {
                    if ((gAudioCtx.notes[i].playbackState.fontId == temporaryCache->entries[0].id) &&
                        gAudioCtx.notes[i].sampleState.bitField0.enabled) {
                        break;
                    }
                }

                if (i == gAudioCtx.numNotes) {
                    AudioLoad_SetFontLoadStatus(temporaryCache->entries[0].id, LOAD_STATUS_DISCARDABLE);
                    loadStatusEntry0 = LOAD_STATUS_DISCARDABLE;
                }
            }

            if (loadStatusEntry1 == LOAD_STATUS_MAYBE_DISCARDABLE) {
                for (i = 0; i < gAudioCtx.numNotes; i++) {
                    if ((gAudioCtx.notes[i].playbackState.fontId == temporaryCache->entries[1].id) &&
                        gAudioCtx.notes[i].sampleState.bitField0.enabled) {
                        break;
                    }
                }

                if (i == gAudioCtx.numNotes) {
                    AudioLoad_SetFontLoadStatus(temporaryCache->entries[1].id, LOAD_STATUS_DISCARDABLE);
                    loadStatusEntry1 = LOAD_STATUS_DISCARDABLE;
                }
            }
        }

        if (loadStatusEntry0 == LOAD_STATUS_NOT_LOADED) {
            temporaryCache->nextSide = 0;
        } else if (loadStatusEntry1 == LOAD_STATUS_NOT_LOADED) {
            temporaryCache->nextSide = 1;
        } else if ((loadStatusEntry0 == LOAD_STATUS_DISCARDABLE) && (loadStatusEntry1 == LOAD_STATUS_DISCARDABLE)) {
            // Use the opposite side from last time.
        } else if (loadStatusEntry0 == LOAD_STATUS_DISCARDABLE) {
            temporaryCache->nextSide = 0;
        } else if (loadStatusEntry1 == LOAD_STATUS_DISCARDABLE) {
            temporaryCache->nextSide = 1;
        } else {
            // @mod Use our seqId getter function
            s32 seqId = AudioApi_GetSeqPlayerSeqId(&gAudioCtx.seqPlayers[i]);

            // Check if there is a side which isn't in active use, if so, evict that one.
            if (tableType == SEQUENCE_TABLE) {
                if (loadStatusEntry0 == LOAD_STATUS_COMPLETE) {
                    for (i = 0; i < gAudioCtx.audioBufferParameters.numSequencePlayers; i++) {
                        if (gAudioCtx.seqPlayers[i].enabled && seqId == temporaryCache->entries[0].id) {
                            break;
                        }
                    }

                    if (i == gAudioCtx.audioBufferParameters.numSequencePlayers) {
                        temporaryCache->nextSide = 0;
                        goto done;
                    }
                }

                if (loadStatusEntry1 == LOAD_STATUS_COMPLETE) {
                    for (i = 0; i < gAudioCtx.audioBufferParameters.numSequencePlayers; i++) {
                        if (gAudioCtx.seqPlayers[i].enabled && seqId == temporaryCache->entries[1].id) {
                            break;
                        }
                    }

                    if (i == gAudioCtx.audioBufferParameters.numSequencePlayers) {
                        temporaryCache->nextSide = 1;
                        goto done;
                    }
                }
            } else if (tableType == FONT_TABLE) {
                if (loadStatusEntry0 == LOAD_STATUS_COMPLETE) {
                    for (i = 0; i < gAudioCtx.numNotes; i++) {
                        if ((gAudioCtx.notes[i].playbackState.fontId == temporaryCache->entries[0].id) &&
                            gAudioCtx.notes[i].sampleState.bitField0.enabled) {
                            break;
                        }
                    }
                    if (i == gAudioCtx.numNotes) {
                        temporaryCache->nextSide = 0;
                        goto done;
                    }
                }

                if (loadStatusEntry1 == LOAD_STATUS_COMPLETE) {
                    for (i = 0; i < gAudioCtx.numNotes; i++) {
                        if ((gAudioCtx.notes[i].playbackState.fontId == temporaryCache->entries[1].id) &&
                            gAudioCtx.notes[i].sampleState.bitField0.enabled) {
                            break;
                        }
                    }
                    if (i == gAudioCtx.numNotes) {
                        temporaryCache->nextSide = 1;
                        goto done;
                    }
                }
            }

            // No such luck. Evict the side that wasn't chosen last time, except
            // if it is being loaded into.
            if (temporaryCache->nextSide == 0) {
                if (loadStatusEntry0 == LOAD_STATUS_IN_PROGRESS) {
                    if (loadStatusEntry1 == LOAD_STATUS_IN_PROGRESS) {
                        goto fail;
                    }
                    temporaryCache->nextSide = 1;
                }
            } else {
                if (loadStatusEntry1 == LOAD_STATUS_IN_PROGRESS) {
                    if (loadStatusEntry0 == LOAD_STATUS_IN_PROGRESS) {
                        goto fail;
                    }
                    temporaryCache->nextSide = 0;
                }
            }

            if (0) {
            fail:
                // Both sides are being loaded into.
                return NULL;
            }
        }
    done:

        side = temporaryCache->nextSide;

        if (temporaryCache->entries[side].id != -1) {
            if (tableType == SAMPLE_TABLE) {
                AudioHeap_DiscardSampleBank(temporaryCache->entries[side].id);
            }

            loadStatus[temporaryCache->entries[side].id] = LOAD_STATUS_NOT_LOADED;

            if (tableType == FONT_TABLE) {
                AudioHeap_DiscardFont(temporaryCache->entries[side].id);
            }
        }

        switch (side) {
            case 0:
                temporaryCache->entries[0].addr = temporaryPool->startAddr;
                temporaryCache->entries[0].id = id;
                temporaryCache->entries[0].size = size;
                temporaryPool->curAddr = temporaryPool->startAddr + size;

                if ((temporaryCache->entries[1].id != -1) &&
                    (temporaryCache->entries[1].addr < temporaryPool->curAddr)) {
                    if (tableType == SAMPLE_TABLE) {
                        AudioHeap_DiscardSampleBank(temporaryCache->entries[1].id);
                    }

                    loadStatus[temporaryCache->entries[1].id] = LOAD_STATUS_NOT_LOADED;

                    switch (tableType) {
                        case SEQUENCE_TABLE:
                            AudioHeap_DiscardSequence((s32)temporaryCache->entries[1].id);
                            break;

                        case FONT_TABLE:
                            AudioHeap_DiscardFont((s32)temporaryCache->entries[1].id);
                            break;
                    }

                    temporaryCache->entries[1].id = -1;
                    temporaryCache->entries[1].addr = temporaryPool->startAddr + temporaryPool->size;
                }

                temporaryAddr = temporaryCache->entries[0].addr;
                break;

            case 1:
                temporaryCache->entries[1].addr =
                    (u8*)((uintptr_t)(temporaryPool->startAddr + temporaryPool->size - size) & ~0xF);
                temporaryCache->entries[1].id = id;
                temporaryCache->entries[1].size = size;
                if ((temporaryCache->entries[0].id != -1) &&
                    (temporaryCache->entries[1].addr < temporaryPool->curAddr)) {
                    if (tableType == SAMPLE_TABLE) {
                        AudioHeap_DiscardSampleBank(temporaryCache->entries[0].id);
                    }

                    loadStatus[temporaryCache->entries[0].id] = LOAD_STATUS_NOT_LOADED;

                    switch (tableType) {
                        case SEQUENCE_TABLE:
                            AudioHeap_DiscardSequence(temporaryCache->entries[0].id);
                            break;

                        case FONT_TABLE:
                            AudioHeap_DiscardFont(temporaryCache->entries[0].id);
                            break;
                    }

                    temporaryCache->entries[0].id = -1;
                    temporaryPool->curAddr = temporaryPool->startAddr;
                }

                temporaryAddr = temporaryCache->entries[1].addr;
                break;

            default:
                return NULL;
        }

        temporaryCache->nextSide ^= 1;
        return temporaryAddr;
    }

    persistentAddr = AudioHeap_Alloc(&loadedCache->persistent.pool, size);
    loadedCache->persistent.entries[loadedCache->persistent.numEntries].addr = persistentAddr;

    if (persistentAddr == NULL) {
        switch (cache) {
            case CACHE_EITHER:
                return AudioHeap_AllocCached(tableType, size, CACHE_TEMPORARY, id);

            case CACHE_TEMPORARY:
            case CACHE_PERSISTENT:
                return NULL;
        }
    }

    loadedCache->persistent.entries[loadedCache->persistent.numEntries].id = id;
    loadedCache->persistent.entries[loadedCache->persistent.numEntries].size = size;

    return loadedCache->persistent.entries[loadedCache->persistent.numEntries++].addr;
}

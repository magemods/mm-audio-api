#include "global.h"
#include "modding.h"
#include "recompdata.h"

/**
 * This file provides various DMA related functions. It is responsible for intercepting DMA requests
 * and returning chunks from mod memory.
 */

#define MK_ASYNC_MSG(retData, tableType, id, loadStatus) \
    (((retData) << 24) | ((tableType) << 16) | ((id) << 8) | (loadStatus))

typedef enum {
    LOAD_STATUS_WAITING,
    LOAD_STATUS_START,
    LOAD_STATUS_LOADING,
    LOAD_STATUS_DONE
} SlowLoadStatus;

typedef struct {
    u16 numInstruments;
    u16 numDrums;
    u16 numSfx;
} UnloadedFonts;

extern DmaHandler sDmaHandler;

extern AudioTable* AudioLoad_GetLoadTable(s32 tableType);
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void* AudioLoad_AsyncLoadInner(s32 tableType, s32 id, s32 nChunks, s32 retData, OSMesgQueue* retQueue);
extern void AudioLoad_FinishAsyncLoad(AudioAsyncLoad* asyncLoad);
extern void AudioLoad_SyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium);
extern void AudioLoad_SyncDmaUnkMedium(uintptr_t devAddr, u8* addr, size_t size, s32 unkMediumParam);
extern void* AudioLoad_SearchCaches(s32 tableType, s32 id);
extern void AudioLoad_SetSampleFontLoadStatusAndApplyCaches(s32 sampleBankId, s32 loadStatus);

/**
 * Normally when data is loaded, it is stored in the audio heap's cache. On subsequent loads if the
 * data is still cached, the AudioLoad_AsyncLoad and AudioLoad_SyncLoad functions will inform the
 * caller that the data was not loaded again. This prevents certain functions from running multiple
 * times on the same data. Since custom audio is always loaded, we just need to keep track if it has
 * previously been requested by the game.
 */
U32HashsetHandle fakeCache;

bool AudioApi_FakeDidAllocate(s32 tableType, s32 realId) {
    if (!fakeCache) {
        fakeCache = recomputil_create_u32_hashset();
    }
    return recomputil_u32_hashset_insert(fakeCache, tableType << 8 | realId);
}

/**
 * While intercepting AudioLoad_Dma works for loading custom audio data, it still takes up space on
 * the audio heap and incurs multiple memcpy / osMesgQueue actions. Instead, we can intercept both
 * AudioLoad_AsyncLoad and AudioLoad_SyncLoad and simply return the pointer to where it is in mod
 * memory. The only audio data that this won't work with is samples, but those are not loaded using
 * these functions.
 *
 * These patches should be able to be safely removed if it causes any problems, but my hope is that
 * it may prevent BGM death since we don't have a limit placed on how many, or how large, custom
 * soundfonts and sequences can be.
 */
RECOMP_PATCH void AudioLoad_AsyncLoad(s32 tableType, s32 id, s32 nChunks, s32 retData, OSMesgQueue* retQueue) {
    AudioTable* table;
    uintptr_t romAddr;
    u32 realId;

    table = AudioLoad_GetLoadTable(tableType);
    realId = AudioLoad_GetRealTableIndex(tableType, id);
    romAddr = table->entries[realId].romAddr;

    // If a ROM address, call normal inner function
    if (!IS_KSEG0(romAddr)) {
        if (AudioLoad_AsyncLoadInner(tableType, id, nChunks, retData, retQueue) == NULL) {
            osSendMesg(retQueue, (OSMesg)0xFFFFFFFF, OS_MESG_NOBLOCK);
        }
        return;
    }

    if (AudioApi_FakeDidAllocate(tableType, realId)) {
        // If this is the first load, call the finish load function
        AudioAsyncLoad asyncLoad = {0};

        asyncLoad.status = LOAD_STATUS_DONE;
        asyncLoad.ramAddr = (u8*)romAddr;
        asyncLoad.retQueue = retQueue;
        asyncLoad.retMsg = MK_ASYNC_MSG(retData, FONT_TABLE, realId, LOAD_STATUS_COMPLETE);

        osCreateMesgQueue(&asyncLoad.msgQueue, &asyncLoad.msg, 1);
        AudioLoad_FinishAsyncLoad(&asyncLoad);
    } else {
        // Otherwise send the os message immediately
        osSendMesg(retQueue, (OSMesg)MK_ASYNC_MSG(retData, 0, 0, LOAD_STATUS_NOT_LOADED), OS_MESG_NOBLOCK);
    }

    switch (tableType) {
    case SEQUENCE_TABLE:
        AudioLoad_SetSeqLoadStatus(realId, LOAD_STATUS_COMPLETE);
        break;
    case FONT_TABLE:
        AudioLoad_SetFontLoadStatus(realId, LOAD_STATUS_COMPLETE);
        break;
    case SAMPLE_TABLE:
        AudioLoad_SetSampleFontLoadStatusAndApplyCaches(realId, LOAD_STATUS_COMPLETE);
        break;
    }
}

RECOMP_PATCH void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate) {
    size_t size;
    AudioTable* table;
    s32 medium2;
    u32 medium;
    s32 loadStatus;
    uintptr_t romAddr;
    s32 cachePolicy;
    void* ramAddr;
    u32 realId;
    s32 mediumUnk = MEDIUM_UNK;

    table = AudioLoad_GetLoadTable(tableType);
    realId = AudioLoad_GetRealTableIndex(tableType, id);
    romAddr = table->entries[realId].romAddr;
    ramAddr = AudioLoad_SearchCaches(tableType, realId);

    if (ramAddr != NULL) {
        *didAllocate = false;
        loadStatus = LOAD_STATUS_COMPLETE;
    } else if (IS_KSEG0(romAddr)) {
        // @mod if it's in memory already, just return the pointer
        *didAllocate = AudioApi_FakeDidAllocate(tableType, realId);
        loadStatus = LOAD_STATUS_COMPLETE;
        ramAddr = (void*)romAddr;
    } else {
        size = table->entries[realId].size;
        size = ALIGN16(size);
        medium = table->entries[id].medium;
        cachePolicy = table->entries[id].cachePolicy;
        switch (cachePolicy) {
            case CACHE_LOAD_PERMANENT:
                //! @bug UB: triggers an UB because this function is missing a return value.
                ramAddr = AudioHeap_AllocPermanent(tableType, realId, size);
                if (ramAddr == NULL) {
                    return ramAddr;
                }
                break;

            case CACHE_LOAD_PERSISTENT:
                ramAddr = AudioHeap_AllocCached(tableType, size, CACHE_PERSISTENT, realId);
                if (ramAddr == NULL) {
                    return ramAddr;
                }
                break;

            case CACHE_LOAD_TEMPORARY:
                ramAddr = AudioHeap_AllocCached(tableType, size, CACHE_TEMPORARY, realId);
                if (ramAddr == NULL) {
                    return ramAddr;
                }
                break;

            case CACHE_LOAD_EITHER:
            case CACHE_LOAD_EITHER_NOSYNC:
                ramAddr = AudioHeap_AllocCached(tableType, size, CACHE_EITHER, realId);
                if (ramAddr == NULL) {
                    return ramAddr;
                }
                break;
        }

        *didAllocate = true;

        medium2 = medium;
        if (medium == MEDIUM_RAM_UNLOADED) {
            if (romAddr == 0) {
                return NULL;
            }

            if (tableType == FONT_TABLE) {
                SoundFont* soundFont = &gAudioCtx.soundFontList[realId];

                soundFont->numInstruments = ((UnloadedFonts*)romAddr)->numInstruments;
                soundFont->numDrums = ((UnloadedFonts*)romAddr)->numDrums;
                soundFont->numSfx = ((UnloadedFonts*)romAddr)->numSfx;
                romAddr += 0x10;
                size -= 0x10;
            }

            bcopy((void*)romAddr, ramAddr, size);
        } else if (medium2 == mediumUnk) {
            AudioLoad_SyncDmaUnkMedium(romAddr, ramAddr, size, (s16)table->header.unkMediumParam);
        } else {
            AudioLoad_SyncDma(romAddr, ramAddr, size, medium);
        }

        loadStatus = (cachePolicy == CACHE_LOAD_PERMANENT) ? LOAD_STATUS_PERMANENT : LOAD_STATUS_COMPLETE;
    }

    switch (tableType) {
        case SEQUENCE_TABLE:
            AudioLoad_SetSeqLoadStatus(realId, loadStatus);
            break;

        case FONT_TABLE:
            AudioLoad_SetFontLoadStatus(realId, loadStatus);
            break;

        case SAMPLE_TABLE:
            AudioLoad_SetSampleFontLoadStatusAndApplyCaches(realId, loadStatus);
            break;

        default:
            break;
    }

    return ramAddr;
}


/**
 * Patch AudioLoad_Dma which will either call AudioApi_Dma_Mod which loads custom audio,
 * or AudioApi_Dma_Rom which loads from the vanilla ROM.
 */
s32 AudioApi_Dma_Mod(OSIoMesg* mesg, u32 priority, s32 direction, uintptr_t devAddr, void* ramAddr,
                     size_t size, OSMesgQueue* reqQueue, s32 medium, const char* dmaFuncType) {
    osSendMesg(reqQueue, NULL, OS_MESG_NOBLOCK);
    Lib_MemCpy(ramAddr, (void*)devAddr, size);
    return 0;
}

s32 AudioApi_Dma_Rom(OSIoMesg* mesg, u32 priority, s32 direction, uintptr_t devAddr, void* ramAddr,
                     size_t size, OSMesgQueue* reqQueue, s32 medium, const char* dmaFuncType) {
    OSPiHandle* handle;

    if (gAudioCtx.resetTimer > 16) {
        return -1;
    }

    switch (medium) {
        case MEDIUM_CART:
            handle = gAudioCtx.cartHandle;
            break;

        case MEDIUM_DISK_DRIVE:
            // driveHandle is uninitialized and corresponds to stubbed-out disk drive support.
            // SM64 Shindou called osDriveRomInit here.
            handle = gAudioCtx.driveHandle;
            break;

        default:
            return 0;
    }

    if ((size % 0x10) != 0) {
        size = ALIGN16(size);
    }

    mesg->hdr.pri = priority;
    mesg->hdr.retQueue = reqQueue;
    mesg->dramAddr = ramAddr;
    mesg->devAddr = devAddr;
    mesg->size = size;
    handle->transferInfo.cmdType = 2;
    sDmaHandler(handle, mesg, direction);
    return 0;
}

RECOMP_PATCH s32 AudioLoad_Dma(OSIoMesg* mesg, u32 priority, s32 direction, uintptr_t devAddr, void* ramAddr,
                               size_t size, OSMesgQueue* reqQueue, s32 medium, const char* dmaFuncType) {
    if (IS_KSEG0(devAddr)) {
        return AudioApi_Dma_Mod(mesg, priority, direction, devAddr, ramAddr, size, reqQueue, medium, dmaFuncType);
    } else {
        return AudioApi_Dma_Rom(mesg, priority, direction, devAddr, ramAddr, size, reqQueue, medium, dmaFuncType);
    }
}


/**
 * This function is patched solely to fix a bug in vanilla MM, only two lines are changed.
 * There are 96 sample DMA buffers. The first 72 are 0x500 in size, and the last 24 are 0x200 in size.
 * But, if you try to load a sample chunk greater than 0x200, there's an integer underflow while checking
 * the buffers that are only 0x200, and the game thinks the chunk has already been loaded.
 */
RECOMP_PATCH void* AudioLoad_DmaSampleData(uintptr_t devAddr, size_t size, s32 arg2, u8* dmaIndexRef, s32 medium) {
    s32 pad1;
    SampleDma* dma;
    s32 hasDma = false;
    uintptr_t dmaDevAddr;
    u32 pad2;
    u32 dmaIndex;
    u32 transfer;
    s32 bufferPos;
    u32 i;

    if (arg2 || (*dmaIndexRef >= gAudioCtx.sampleDmaListSize1)) {
        for (i = gAudioCtx.sampleDmaListSize1; i < gAudioCtx.sampleDmaCount; i++) {
            dma = &gAudioCtx.sampleDmas[i];
            bufferPos = devAddr - dma->devAddr;
            // @mod add (dma->size >= size) check
            if ((0 <= bufferPos) && (dma->size >= size) && ((u32)bufferPos <= (dma->size - size))) {
                // We already have a DMA request for this memory range.
                if ((dma->ttl == 0) && (gAudioCtx.sampleDmaReuseQueue2RdPos != gAudioCtx.sampleDmaReuseQueue2WrPos)) {
                    // Move the DMA out of the reuse queue, by swapping it with the
                    // read pos, and then incrementing the read pos.
                    if (dma->reuseIndex != gAudioCtx.sampleDmaReuseQueue2RdPos) {
                        gAudioCtx.sampleDmaReuseQueue2[dma->reuseIndex] =
                            gAudioCtx.sampleDmaReuseQueue2[gAudioCtx.sampleDmaReuseQueue2RdPos];
                        gAudioCtx.sampleDmas[gAudioCtx.sampleDmaReuseQueue2[gAudioCtx.sampleDmaReuseQueue2RdPos]]
                            .reuseIndex = dma->reuseIndex;
                    }
                    gAudioCtx.sampleDmaReuseQueue2RdPos++;
                }
                dma->ttl = 32;
                *dmaIndexRef = (u8)i;
                return dma->ramAddr + (devAddr - dma->devAddr);
            }
        }

        if (!arg2) {
            goto search_short_lived;
        }

        if ((gAudioCtx.sampleDmaReuseQueue2RdPos != gAudioCtx.sampleDmaReuseQueue2WrPos) && arg2) {
            // Allocate a DMA from reuse queue 2, unless full.
            dmaIndex = gAudioCtx.sampleDmaReuseQueue2[gAudioCtx.sampleDmaReuseQueue2RdPos];
            gAudioCtx.sampleDmaReuseQueue2RdPos++;
            dma = gAudioCtx.sampleDmas + dmaIndex;
            hasDma = true;
        }
    } else {
    search_short_lived:
        dma = gAudioCtx.sampleDmas + *dmaIndexRef;
        i = 0;
    again:
        bufferPos = devAddr - dma->devAddr;
        // @mod add (dma->size >= size) check
        if (0 <= bufferPos && (dma->size >= size) && (u32)bufferPos <= dma->size - size) {
            // We already have DMA for this memory range.
            if (dma->ttl == 0) {
                // Move the DMA out of the reuse queue, by swapping it with the
                // read pos, and then incrementing the read pos.
                if (dma->reuseIndex != gAudioCtx.sampleDmaReuseQueue1RdPos) {
                    gAudioCtx.sampleDmaReuseQueue1[dma->reuseIndex] =
                        gAudioCtx.sampleDmaReuseQueue1[gAudioCtx.sampleDmaReuseQueue1RdPos];
                    gAudioCtx.sampleDmas[gAudioCtx.sampleDmaReuseQueue1[gAudioCtx.sampleDmaReuseQueue1RdPos]]
                        .reuseIndex = dma->reuseIndex;
                }
                gAudioCtx.sampleDmaReuseQueue1RdPos++;
            }
            dma->ttl = 2;
            return dma->ramAddr + (devAddr - dma->devAddr);
        }
        dma = gAudioCtx.sampleDmas + i++;
        if (i <= gAudioCtx.sampleDmaListSize1) {
            goto again;
        }
    }

    if (!hasDma) {
        if (gAudioCtx.sampleDmaReuseQueue1RdPos == gAudioCtx.sampleDmaReuseQueue1WrPos) {
            return NULL;
        }
        // Allocate a DMA from reuse queue 1.
        dmaIndex = gAudioCtx.sampleDmaReuseQueue1[gAudioCtx.sampleDmaReuseQueue1RdPos++];
        dma = gAudioCtx.sampleDmas + dmaIndex;
        hasDma = true;
    }

    transfer = dma->size;
    dmaDevAddr = devAddr & ~0xF;
    dma->ttl = 3;
    dma->devAddr = dmaDevAddr;
    dma->sizeUnused = transfer;
    AudioLoad_Dma(&gAudioCtx.currAudioFrameDmaIoMesgBuf[gAudioCtx.curAudioFrameDmaCount++],
                  OS_MESG_PRI_NORMAL, OS_READ, dmaDevAddr, dma->ramAddr, transfer,
                  &gAudioCtx.curAudioFrameDmaQueue, medium, "SUPERDMA");
    *dmaIndexRef = dmaIndex;
    return (devAddr - dmaDevAddr) + dma->ramAddr;
}

#include "load.h"
#include "modding.h"
#include "recomputils.h"
#include "heap.h"

/**
 * This file is responsible for intercepting load requests and returning data from mod memory.
 */

#define MK_ASYNC_MSG(retData, tableType, id, loadStatus) \
    (((retData) << 24) | ((tableType) << 16) | ((id) << 8) | (loadStatus))
#define ASYNC_TBLTYPE(v) ((u8)(v >> 16))
#define ASYNC_ID(v) ((u8)(v >> 8))
#define ASYNC_STATUS(v) ((u8)(v >> 0))

typedef enum {
    LOAD_STATUS_WAITING,
    LOAD_STATUS_START,
    LOAD_STATUS_LOADING,
    LOAD_STATUS_DONE
} SlowLoadStatus;

extern DmaHandler sDmaHandler;

OSIoMesg currAudioFrameDmaIoMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];
OSMesg currAudioFrameDmaMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];
u8* sExtSeqLoadStatus = gAudioCtx.seqLoadStatus;

extern AudioTable* AudioLoad_GetLoadTable(s32 tableType);
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void AudioLoad_FinishAsyncLoad(AudioAsyncLoad* asyncLoad);
extern void AudioLoad_SyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium);
extern AudioAsyncLoad* AudioLoad_StartAsyncLoad(uintptr_t devAddr, void* ramAddr, size_t size, s32 medium,
                                                s32 nChunks, OSMesgQueue* retQueue, s32 retMsg);

RECOMP_DECLARE_EVENT(AudioApi_SequenceLoadedInternal(s32 seqId, void** ramAddrPtr));
RECOMP_DECLARE_EVENT(AudioApi_SoundFontLoadedInternal(s32 fontId, void** ramAddrPtr));


// ======== LOAD STATUS FUNCTIONS ========

s32 AudioApi_GetTableEntryLoadStatus(s32 tableType, u32 id) {
    if ((id & 0xFF) == 0xFF || (id & 0xFF) == 0xFE) return LOAD_STATUS_PERMANENT;

    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    if (tableType == SEQUENCE_TABLE) {
        return sExtSeqLoadStatus[realId];
    } else if (tableType == FONT_TABLE) {
        return gAudioCtx.fontLoadStatus[realId];
    } else {
        return LOAD_STATUS_NOT_LOADED;
    }
}

void AudioApi_SetTableEntryLoadStatus(s32 tableType, s32 id, s32 status) {
    if ((id & 0xFF) == 0xFF || (id & 0xFF) == 0xFE) return;
    if (status == LOAD_STATUS_NOT_LOADED || status == LOAD_STATUS_DISCARDABLE
        || status == LOAD_STATUS_MAYBE_DISCARDABLE) return;

    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    if (tableType == SEQUENCE_TABLE) {
        sExtSeqLoadStatus[realId] = status;
    } else if (tableType == FONT_TABLE) {
        gAudioCtx.fontLoadStatus[realId] = status;
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

RECOMP_PATCH void* AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id) {
    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    uintptr_t romAddr = table->entries[realId].romAddr;
    return IS_KSEG0(romAddr) ? (void*)romAddr : NULL;
}

RECOMP_PATCH void* AudioHeap_SearchRegularCaches(s32 tableType, s32 cache, s32 id) {
    return AudioHeap_SearchCaches(tableType, 0, id);
}

RECOMP_PATCH void* AudioHeap_SearchPermanentCache(s32 tableType, s32 id) {
    return AudioHeap_SearchCaches(tableType, 0, id);
}


// ======== LOAD FUNCTIONS ========

/**
 * While intercepting AudioLoad_Dma works for loading custom audio data, it still takes up space on
 * the audio heap and incurs multiple memcpy / osMesgQueue actions. Instead, we can intercept both
 * AudioLoad_AsyncLoad and AudioLoad_SyncLoad and simply return the pointer to where it is in mod
 * memory. The only audio data that this won't work with is samples, but those are not loaded using
 * these functions.
 */
RECOMP_PATCH void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate) {
    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    uintptr_t romAddr = table->entries[realId].romAddr;

    // Certain functions such as `AudioLoad_RelocateFont` will run each time data is loaded from
    // the ROM. Since after the first load we persist the data in memory, we will only set this
    // value to true if this is the first load.
    *didAllocate = AudioApi_GetTableEntryLoadStatus(tableType, realId) == LOAD_STATUS_NOT_LOADED;

    // If it's in memory already, just return the pointer
    if (IS_KSEG0(romAddr)) {
        AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_COMPLETE);
        return (void*)romAddr;
    }

    size_t size = ALIGN16(table->entries[realId].size);
    u32 medium = table->entries[id].medium;

    // Allocate temporary memory in the audio heap for the DMA process
    void* ramAddr = AudioHeap_LoadBufferAlloc(tableType, realId, size);
    if (ramAddr == NULL) return NULL;

    AudioLoad_SyncDma(romAddr, ramAddr, size, medium);
    AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_COMPLETE);

    // For sequences, the following event is where the data will be copied into mod memory.
    // For soundfonts, the following event currently does nothing.
    if (tableType == SEQUENCE_TABLE) {
        AudioApi_SequenceLoadedInternal(realId, &ramAddr);
    } else if (tableType == FONT_TABLE) {
        AudioApi_SoundFontLoadedInternal(realId, &ramAddr);
    }

    // Free the memory from the audio heap. Note that soundfonts have not yet been copied to mod memory.
    // However that will happen before any other data has a chance to be written to that space.
    AudioHeap_LoadBufferFree(tableType, realId);
    return ramAddr;
}

RECOMP_PATCH void AudioLoad_AsyncLoad(s32 tableType, s32 id, s32 nChunks, s32 retData, OSMesgQueue* retQueue) {
    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    uintptr_t romAddr = table->entries[realId].romAddr;

    if (IS_KSEG0(romAddr)) {
        if (AudioApi_GetTableEntryLoadStatus(tableType, realId) == LOAD_STATUS_NOT_LOADED) {
            // If this is the first time loading the data, but it is already in memory, we will just
            // call `AudioLoad_FinishAsyncLoad` directly.
            AudioAsyncLoad asyncLoad = {0};
            asyncLoad.status = LOAD_STATUS_DONE;
            asyncLoad.ramAddr = (u8*)romAddr;
            asyncLoad.retQueue = retQueue;
            asyncLoad.retMsg = MK_ASYNC_MSG(retData, tableType, realId, LOAD_STATUS_COMPLETE);

            osCreateMesgQueue(&asyncLoad.msgQueue, &asyncLoad.msg, 1);
            AudioLoad_FinishAsyncLoad(&asyncLoad);
        } else {
            // Otherwise, we send a message to prevent blocking.
            osSendMesg(retQueue, (OSMesg)MK_ASYNC_MSG(retData, 0, 0, LOAD_STATUS_NOT_LOADED), OS_MESG_NOBLOCK);
        }
        AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_COMPLETE);
        return;
    }

    size_t size = ALIGN16(table->entries[realId].size);
    u32 medium = table->entries[id].medium;

    void* ramAddr = AudioHeap_LoadBufferAlloc(tableType, realId, size);
    if (ramAddr == NULL) {
        osSendMesg(retQueue, (OSMesg)0xFFFFFFFF, OS_MESG_NOBLOCK);
        return;
    }

    AudioLoad_StartAsyncLoad(romAddr, ramAddr, size, medium, nChunks, retQueue,
                             MK_ASYNC_MSG(retData, tableType, realId, LOAD_STATUS_COMPLETE));
    AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_IN_PROGRESS);
}

RECOMP_HOOK("AudioLoad_FinishAsyncLoad") void onAudioLoad_FinishAsyncLoad(AudioAsyncLoad* asyncLoad) {
    u32 retMsg = asyncLoad->retMsg;
    s32 tableType = ASYNC_TBLTYPE(retMsg);
    u32 realId = ASYNC_ID(retMsg);

    // See notes from `AudioLoad_SyncLoad`. Note that we again free soundfont memory from the heap
    // before it's copied to mod memory, but that will happen before it's overwritten.
    if (tableType == SEQUENCE_TABLE) {
        AudioApi_SequenceLoadedInternal(realId, (void*)&asyncLoad->ramAddr);
    } else if (tableType == FONT_TABLE) {
        AudioApi_SoundFontLoadedInternal(realId, (void*)&asyncLoad->ramAddr);
    }

    AudioHeap_LoadBufferFree(tableType, realId);
}

RECOMP_PATCH u8* AudioLoad_SyncLoadSeq(s32 seqId) {
    s32 didAllocate;
    if (AudioApi_GetTableEntryLoadStatus(SEQUENCE_TABLE, seqId) == LOAD_STATUS_IN_PROGRESS) {
        return NULL;
    }
    return AudioLoad_SyncLoad(SEQUENCE_TABLE, seqId, &didAllocate);
}


// ======== DMA FUNCTIONS ========

s32 AudioApi_Dma_Mod(OSIoMesg* mesg, u32 priority, s32 direction, uintptr_t devAddr, void* ramAddr,
                     size_t size, OSMesgQueue* reqQueue, s32 medium, const char* dmaFuncType) {
    if (reqQueue) osSendMesg(reqQueue, NULL, OS_MESG_NOBLOCK);
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

RECOMP_PATCH void* AudioLoad_DmaSampleData(uintptr_t devAddr, size_t size, s32 arg2, u8* dmaIndexRef, s32 medium) {
    bool didAllocate;
    u8* ramAddr = AudioApi_RspCacheAlloc((void*)devAddr, size, &didAllocate);

    if (didAllocate) {
        if (IS_KSEG0(devAddr)) {
            // If the sample is in RAM, call AudioLoad_Dma_Rom directly since there are a limited
            // number of entries in the message queue.
            AudioApi_Dma_Mod(NULL, 0, 0, devAddr, ramAddr, size, NULL, 0, "");
        } else {
            if (gAudioCtx.curAudioFrameDmaCount + 1 >= MAX_SAMPLE_DMA_PER_FRAME) {
                return NULL;
            }
            AudioApi_Dma_Rom(&currAudioFrameDmaIoMesgBuf[gAudioCtx.curAudioFrameDmaCount++], OS_MESG_PRI_NORMAL,
                             OS_READ, devAddr, ramAddr, size, &gAudioCtx.curAudioFrameDmaQueue, medium, "SUPERDMA");
        }
    }

    return ramAddr;
}

#include <core/load.h>
#include <recomp/modding.h>
#include <recomp/recomputils.h>
#include <utils/dynamicdataarray.h>
#include <core/load_status.h>
#include <core/heap.h>

/**
 * This file is responsible for intercepting load requests and returning data from mod memory.
 */

#define MK_ASYNC_MSG(retData, tableType, id, loadStatus) \
    (((retData) << 24) | ((tableType) << 16) | ((id) << 8) | (loadStatus))
#define ASYNC_TBLTYPE(v) ((u8)(v >> 16))
#define ASYNC_ID(v) ((u8)(v >> 8))
#define ASYNC_STATUS(v) ((u8)(v >> 0))

#define DMA_CALLBACK_DEFAULT_CAPACITY 32

typedef struct AudioApiDmaCallbackEntry {
    AudioApiDmaCallback callback;
    u32 arg0;
    u32 arg1;
    u32 arg2;
} AudioApiDmaCallbackEntry;

extern DmaHandler sDmaHandler;

DynamicDataArray dmaCallbacks;
OSIoMesg currAudioFrameDmaIoMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];
OSMesg currAudioFrameDmaMesgBuf[MAX_SAMPLE_DMA_PER_FRAME];

extern AudioTable* AudioLoad_GetLoadTable(s32 tableType);
extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
extern void* AudioLoad_SearchCaches(s32 tableType, s32 id);
extern void AudioLoad_FinishAsyncLoad(AudioAsyncLoad* asyncLoad);
extern void AudioLoad_SyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium);
extern AudioAsyncLoad* AudioLoad_StartAsyncLoad(uintptr_t devAddr, void* ramAddr, size_t size, s32 medium,
                                                s32 nChunks, OSMesgQueue* retQueue, s32 retMsg);

s32 AudioApi_Dma_Callback(uintptr_t devAddr, void* ramAddr, size_t size, size_t offset);

RECOMP_DECLARE_EVENT(AudioApi_SequenceLoadedInternal(s32 seqId, void** ramAddrPtr));
RECOMP_DECLARE_EVENT(AudioApi_SoundFontLoadedInternal(s32 fontId, void** ramAddrPtr));

RECOMP_IMPORT(".", bool AudioApiNative_Dma(s16* buf, u32 size, u32 offset, u32* args));

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_LoadInit() {
    DynDataArr_init(&dmaCallbacks, sizeof(AudioApiDmaCallbackEntry), DMA_CALLBACK_DEFAULT_CAPACITY);
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
    s32 cachePolicy = table->entries[id].cachePolicy;
    size_t size = ALIGN16(table->entries[realId].size);
    u32 medium = table->entries[id].medium;
    void* ramAddr;

    // Make sure we don't have any in progress loads for this entry
    if (AudioApi_GetTableEntryLoadStatus(tableType, realId) == LOAD_STATUS_IN_PROGRESS) {
        *didAllocate = false;
        return NULL;
    }

    // Certain functions such as `AudioLoad_RelocateFont` will run each time data is loaded from
    // the ROM. Since after the first load we persist the data in memory, we will only set this
    // value to true if this is the first load.
    *didAllocate = AudioLoad_SearchCaches(tableType, realId) == NULL;

    if (IS_KSEG0(romAddr)) {
        // If it's in memory already, just return the pointer
        ramAddr = (void*)romAddr;
        AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_COMPLETE);
    }
    else if (IS_DMA_CALLBACK_DEV_ADDR(romAddr)) {
        // Allocate memory for the callback DMA process
        ramAddr = recomp_alloc(size);
        if (ramAddr == NULL) {
            return NULL;
        }
        AudioApi_Dma_Callback(romAddr, ramAddr, size, 0);
    }
    else {
        // Allocate temporary memory in the audio heap for the DMA process
        ramAddr = AudioHeap_LoadBufferAlloc(tableType, realId, size);
        if (ramAddr == NULL) {
            return NULL;
        }
        AudioLoad_SyncDma(romAddr, ramAddr, size, medium);
    }

    AudioApi_SetTableEntryLoadStatus(tableType, realId, LOAD_STATUS_COMPLETE);
    AudioApi_PushFakeCache(tableType, cachePolicy, realId);

    if (*didAllocate) {
        if (tableType == SEQUENCE_TABLE) {
            AudioApi_SequenceLoadedInternal(realId, &ramAddr);
        } else if (tableType == FONT_TABLE) {
            AudioApi_SoundFontLoadedInternal(realId, &ramAddr);
        }
    }

    return ramAddr;
}

RECOMP_PATCH void AudioLoad_AsyncLoad(s32 tableType, s32 id, s32 nChunks, s32 retData, OSMesgQueue* retQueue) {
    AudioTable* table = AudioLoad_GetLoadTable(tableType);
    u32 realId = AudioLoad_GetRealTableIndex(tableType, id);
    uintptr_t romAddr = table->entries[realId].romAddr;
    s32 cachePolicy = table->entries[id].cachePolicy;
    size_t size = ALIGN16(table->entries[realId].size);
    u32 medium = table->entries[id].medium;
    s32 loadStatus;
    void* ramAddr;

    // Make sure we don't have any in progress loads for this entry
    if (AudioApi_GetTableEntryLoadStatus(tableType, realId) == LOAD_STATUS_IN_PROGRESS) {
        osSendMesg(retQueue, (OSMesg)MK_ASYNC_MSG(retData, 0, 0, LOAD_STATUS_NOT_LOADED), OS_MESG_NOBLOCK);
        return;
    }

    if (IS_KSEG0(romAddr)) {
        if (AudioLoad_SearchCaches(tableType, realId) == NULL) {
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
        loadStatus = LOAD_STATUS_COMPLETE;
    }
    else {
        ramAddr = AudioHeap_LoadBufferAlloc(tableType, realId, size);
        if (ramAddr == NULL) {
            osSendMesg(retQueue, (OSMesg)0xFFFFFFFF, OS_MESG_NOBLOCK);
            return;
        }

        if (IS_DMA_CALLBACK_DEV_ADDR(romAddr)) {
            nChunks = 1;
        }

        AudioLoad_StartAsyncLoad(romAddr, ramAddr, size, medium, nChunks, retQueue,
                                 MK_ASYNC_MSG(retData, tableType, realId, LOAD_STATUS_COMPLETE));
        loadStatus = LOAD_STATUS_IN_PROGRESS;
    }

    AudioApi_SetTableEntryLoadStatus(tableType, realId, loadStatus);
    AudioApi_PushFakeCache(tableType, cachePolicy, realId);
}

RECOMP_HOOK("AudioLoad_FinishAsyncLoad") void onAudioLoad_FinishAsyncLoad(AudioAsyncLoad* asyncLoad) {
    u32 retMsg = asyncLoad->retMsg;
    u8 tableType = ASYNC_TBLTYPE(retMsg);
    u8 realId = ASYNC_ID(retMsg);
    u8 status = ASYNC_STATUS(retMsg);

    if (status == LOAD_STATUS_COMPLETE) {
        if (tableType == SEQUENCE_TABLE) {
            AudioApi_SequenceLoadedInternal(realId, (void*)&asyncLoad->ramAddr);
        } else if (tableType == FONT_TABLE) {
            AudioApi_SoundFontLoadedInternal(realId, (void*)&asyncLoad->ramAddr);
        }
    }
}

RECOMP_PATCH u8* AudioLoad_SyncLoadSeq(s32 seqId) {
    s32 didAllocate;
    return AudioLoad_SyncLoad(SEQUENCE_TABLE, seqId, &didAllocate);
}


// ======== DMA FUNCTIONS ========

RECOMP_EXPORT uintptr_t AudioApi_AddDmaCallback(AudioApiDmaCallback callback, u32 arg0, u32 arg1, u32 arg2) {
    u16 id = dmaCallbacks.count;
    AudioApiDmaCallbackEntry entry = { callback, arg0, arg1, arg2 };

    DynDataArr_push(&dmaCallbacks, &entry);

    return DMA_CALLBACK_START_DEV_ADDR + id;
}

RECOMP_EXPORT s32 AudioApi_NativeDmaCallback(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1, u32 arg2) {
    u32 args[] = {arg0, arg1, arg2};

    if (!AudioApiNative_Dma(ramAddr, size, offset, args)) {
        return -1;
    }

    return 0;
}

s32 AudioApi_Dma_Callback(uintptr_t devAddr, void* ramAddr, size_t size, size_t offset) {
    u16 id = devAddr - DMA_CALLBACK_START_DEV_ADDR;

    if (gAudioCtx.resetTimer > 16) {
        return -1;
    }
    if (id >= (u16)dmaCallbacks.count) {
        return -1;
    }

    AudioApiDmaCallbackEntry* entry = DynDataArr_get(&dmaCallbacks, id);
    return entry->callback(ramAddr, size, offset, entry->arg0, entry->arg1, entry->arg2);
}

s32 AudioApi_Dma_Mod(uintptr_t devAddr, void* ramAddr, size_t size) {
    if (gAudioCtx.resetTimer > 16) {
        return -1;
    }

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
        osSendMesg(reqQueue, NULL, OS_MESG_NOBLOCK);
        return AudioApi_Dma_Mod(devAddr, ramAddr, size);
    }
    if (IS_DMA_CALLBACK_DEV_ADDR(devAddr)) {
        osSendMesg(reqQueue, NULL, OS_MESG_NOBLOCK);
        return AudioApi_Dma_Callback(devAddr, ramAddr, size, 0);
    }
    return AudioApi_Dma_Rom(mesg, priority, direction, devAddr, ramAddr, size, reqQueue, medium, dmaFuncType);
}

RECOMP_PATCH void* AudioLoad_DmaSampleData(uintptr_t devAddr, size_t size, s32 arg2, u8* dmaIndexRef, s32 medium) {
    uintptr_t dmaDevAddr;
    size_t dmaSize;
    u8* ramAddr;
    s32 result;

    if (IS_KSEG0(devAddr)) {
        ramAddr = AudioApi_RspCacheSearch((void*)devAddr, size);
        if (ramAddr) {
            return ramAddr;
        }
        ramAddr = AudioApi_RspCacheAlloc((void*)devAddr, size, 0);
        if (!ramAddr) {
            return NULL;
        }
        result = AudioApi_Dma_Mod(devAddr, ramAddr, size);
        if (result != 0) {
            AudioApi_RspCacheInvalidateLastEntry();
            return NULL;
        }
        return ramAddr;
    }

    if (IS_DMA_CALLBACK_DEV_ADDR(devAddr)) {
        ramAddr = AudioApi_RspCacheOffsetSearch((void*)devAddr, size * SAMPLE_SIZE, arg2 * SAMPLE_SIZE);
        if (ramAddr) {
            return ramAddr;
        }
        ramAddr = AudioApi_RspCacheAlloc((void*)devAddr, size * SAMPLE_SIZE, arg2 * SAMPLE_SIZE);
        if (!ramAddr) {
            return NULL;
        }
        result = AudioApi_Dma_Callback(devAddr, ramAddr, size, arg2);
        if (result != 0) {
            AudioApi_RspCacheInvalidateLastEntry();
            return NULL;
        }
        return ramAddr;
    }

    // Vanilla game does not have this check, meaning the message buffer array can overflow
    // causing the audio thread to softlock.
    if (gAudioCtx.curAudioFrameDmaCount + 1 >= MAX_SAMPLE_DMA_PER_FRAME) {
        return NULL;
    }

    dmaDevAddr = devAddr & ~0xF;
    dmaSize = size + (devAddr & 0xF);

    ramAddr = AudioApi_RspCacheSearch((void*)dmaDevAddr, dmaSize);
    if (ramAddr) {
        return (devAddr - dmaDevAddr) + ramAddr;
    }

    ramAddr = AudioApi_RspCacheAlloc((void*)dmaDevAddr, dmaSize, 0);
    if (!ramAddr) {
        return NULL;
    }

    result = AudioApi_Dma_Rom(&currAudioFrameDmaIoMesgBuf[gAudioCtx.curAudioFrameDmaCount++],
                              OS_MESG_PRI_NORMAL, OS_READ, dmaDevAddr, ramAddr, dmaSize,
                              &gAudioCtx.curAudioFrameDmaQueue, medium, "SUPERDMA");
    if (result != 0) {
        AudioApi_RspCacheInvalidateLastEntry();
        return NULL;
    }

    return (devAddr - dmaDevAddr) + ramAddr;
}

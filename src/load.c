#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "queue.h"

// Internal events
RECOMP_DECLARE_EVENT(AudioApi_InitInternal());
RECOMP_DECLARE_EVENT(AudioApi_ReadyInternal());

// Public events
RECOMP_DECLARE_EVENT(AudioApi_Init());
RECOMP_DECLARE_EVENT(AudioApi_Ready());
RECOMP_DECLARE_EVENT(AudioApi_AfterSyncDma(uintptr_t devAddr, u8* ramAddr));

extern DmaHandler sDmaHandler;
extern u8 gAudioHeap[0x138000];
extern void AudioLoad_InitTable(AudioTable* table, uintptr_t romAddr, u16 unkMediumParam);
extern void AudioLoad_InitSoundFont(s32 fontId);

/* -------------------------------------------------------------------------- */

RECOMP_PATCH void AudioLoad_Init(void* heap, size_t heapSize) {
    s32 pad1[9];
    s32 numFonts;
    s32 pad2[2];
    u8* audioCtxPtr;
    void* addr;
    s32 i;
    s32 j;

    gAudioCustomUpdateFunction = NULL;
    gAudioCustomReverbFunction = NULL;
    gAudioCustomSynthFunction = NULL;

    for (i = 0; i < ARRAY_COUNT(gAudioCtx.customSeqFunctions); i++) {
        gAudioCtx.customSeqFunctions[i] = NULL;
    }

    gAudioCtx.resetTimer = 0;
    gAudioCtx.unk_29B8 = false;

    // Set all of gAudioCtx to 0
    audioCtxPtr = (u8*)&gAudioCtx;
    for (j = sizeof(gAudioCtx); j >= 0; j--) {
        *audioCtxPtr++ = 0;
    }

    switch (osTvType) {
        case OS_TV_PAL:
            gAudioCtx.unk_2960 = 20.03042f;
            gAudioCtx.refreshRate = 50;
            break;

        case OS_TV_MPAL:
            gAudioCtx.unk_2960 = 16.546f;
            gAudioCtx.refreshRate = 60;
            break;

        case OS_TV_NTSC:
        default:
            gAudioCtx.unk_2960 = 16.713f;
            gAudioCtx.refreshRate = 60;
            break;
    }

    AudioThread_InitMesgQueues();

    for (i = 0; i < ARRAY_COUNT(gAudioCtx.numSamplesPerFrame); i++) {
        gAudioCtx.numSamplesPerFrame[i] = 0xA0;
    }

    gAudioCtx.totalTaskCount = 0;
    gAudioCtx.rspTaskIndex = 0;
    gAudioCtx.curAiBufferIndex = 0;
    gAudioCtx.soundMode = SOUNDMODE_STEREO;
    gAudioCtx.curTask = NULL;
    gAudioCtx.rspTask[0].task.t.data_size = 0;
    gAudioCtx.rspTask[1].task.t.data_size = 0;

    osCreateMesgQueue(&gAudioCtx.syncDmaQueue, &gAudioCtx.syncDmaMesg, 1);
    osCreateMesgQueue(&gAudioCtx.curAudioFrameDmaQueue, gAudioCtx.currAudioFrameDmaMesgBuf,
                      ARRAY_COUNT(gAudioCtx.currAudioFrameDmaMesgBuf));
    osCreateMesgQueue(&gAudioCtx.externalLoadQueue, gAudioCtx.externalLoadMesgBuf,
                      ARRAY_COUNT(gAudioCtx.externalLoadMesgBuf));
    osCreateMesgQueue(&gAudioCtx.preloadSampleQueue, gAudioCtx.preloadSampleMesgBuf,
                      ARRAY_COUNT(gAudioCtx.preloadSampleMesgBuf));
    gAudioCtx.curAudioFrameDmaCount = 0;
    gAudioCtx.sampleDmaCount = 0;
    gAudioCtx.cartHandle = osCartRomInit();

    if (heap == NULL) {
        gAudioCtx.audioHeap = gAudioHeap;
        gAudioCtx.audioHeapSize = gAudioHeapInitSizes.heapSize;
    } else {
        void** hp = &heap;

        gAudioCtx.audioHeap = *hp;
        gAudioCtx.audioHeapSize = heapSize;
    }

    for (i = 0; i < ((s32)gAudioCtx.audioHeapSize / (s32)sizeof(u64)); i++) {
        ((u64*)gAudioCtx.audioHeap)[i] = 0;
    }

    // Main Pool Split (split entirety of audio heap into initPool and sessionPool)
    AudioHeap_InitMainPool(gAudioHeapInitSizes.initPoolSize);

    // Initialize the audio interface buffers
    for (i = 0; i < ARRAY_COUNT(gAudioCtx.aiBuffers); i++) {
        gAudioCtx.aiBuffers[i] = AudioHeap_AllocZeroed(&gAudioCtx.initPool, AIBUF_LEN * sizeof(s16));
    }

    // Connect audio tables to their tables in memory
    gAudioCtx.sequenceTable = (AudioTable*)gSequenceTable;
    gAudioCtx.soundFontTable = &gSoundFontTable;
    gAudioCtx.sampleBankTable = &gSampleBankTable;
    gAudioCtx.sequenceFontTable = gSequenceFontTable;

    // Initialize audio tables
    AudioLoad_InitTable(gAudioCtx.sequenceTable, SEGMENT_ROM_START(Audioseq), 0);
    AudioLoad_InitTable(gAudioCtx.soundFontTable, SEGMENT_ROM_START(Audiobank), 0);
    AudioLoad_InitTable(gAudioCtx.sampleBankTable, SEGMENT_ROM_START(Audiotable), 0);

    // @mod Dispatch internal event for API to initialize tables
    AudioApi_InitInternal();

    // @mod Dispatch event for mods to queue audio changes
    gAudioApiInitPhase = AUDIOAPI_INIT_QUEUEING;
    AudioApi_Init();

    // @mod Dispatch internal event for API to drain queues
    gAudioApiInitPhase = AUDIOAPI_INIT_QUEUED;
    AudioApi_ReadyInternal();

    // @mod Dispatch event for mods to potentially interact with other mods
    gAudioApiInitPhase = AUDIOAPI_INIT_READY;
    AudioApi_Ready();

    gAudioCtx.numSequences = gAudioCtx.sequenceTable->header.numEntries;

    gAudioCtx.specId = 0;
    gAudioCtx.resetStatus = 1; // Set reset to immediately initialize the audio heap
    AudioHeap_ResetStep();

    numFonts = gAudioCtx.soundFontTable->header.numEntries;
    gAudioCtx.soundFontList = AudioHeap_Alloc(&gAudioCtx.initPool, numFonts * sizeof(SoundFont));

    for (i = 0; i < numFonts; i++) {
        AudioLoad_InitSoundFont(i);
    }

    if (addr = AudioHeap_Alloc(&gAudioCtx.initPool, gAudioHeapInitSizes.permanentPoolSize), addr == NULL) {
        gAudioHeapInitSizes.permanentPoolSize = 0;
    }

    AudioHeap_InitPool(&gAudioCtx.permanentPool, addr, gAudioHeapInitSizes.permanentPoolSize);
    gAudioCtxInitialized = true;
    osSendMesg(gAudioCtx.taskStartQueueP, (void*)gAudioCtx.totalTaskCount, OS_MESG_NOBLOCK);
}

/* -------------------------------------------------------------------------- */

// Our custom function
s32 AudioApi_Dma_Mod(OSIoMesg* mesg, u32 priority, s32 direction, uintptr_t devAddr, void* ramAddr,
                     size_t size, OSMesgQueue* reqQueue, s32 medium, const char* dmaFuncType) {
      osSendMesg(reqQueue, NULL, OS_MESG_NOBLOCK);
      Lib_MemCpy(ramAddr, (void*)devAddr, size);
      return 0;
}

// The original function
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

/* -------------------------------------------------------------------------- */

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
    AudioLoad_Dma(&gAudioCtx.currAudioFrameDmaIoMesgBuf[gAudioCtx.curAudioFrameDmaCount++], OS_MESG_PRI_NORMAL, OS_READ,
                  dmaDevAddr, dma->ramAddr, transfer, &gAudioCtx.curAudioFrameDmaQueue, medium, "SUPERDMA");
    *dmaIndexRef = dmaIndex;
    return (devAddr - dmaDevAddr) + dma->ramAddr;
}

/* -------------------------------------------------------------------------- */

static uintptr_t sDevAddr = 0;
static u8* sRamAddr = NULL;

RECOMP_HOOK("AudioLoad_SyncDma") void AudioLoad_onSyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium) {
    sDevAddr = devAddr;
    sRamAddr = ramAddr;
}

RECOMP_HOOK_RETURN("AudioLoad_SyncDma") void AudioLoad_afterSyncDma() {
    AudioApi_AfterSyncDma(sDevAddr, sRamAddr);
}

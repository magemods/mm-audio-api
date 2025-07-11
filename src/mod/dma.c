#include "global.h"
#include "modding.h"

/**
 * This file provides various DMA related functions. It is responsible for intercepting DMA requests
 * and returning chunks from mod memory.
 */

extern DmaHandler sDmaHandler;

uintptr_t sDevAddr = 0;
u8* sRamAddr = NULL;

RECOMP_DECLARE_EVENT(AudioApi_AfterSyncDma(uintptr_t devAddr, u8* ramAddr));

/**
 * Dispatch an event whenever a synchronous audio DMA request is completed
 */
RECOMP_HOOK("AudioLoad_SyncDma") void AudioLoad_onSyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium) {
    sDevAddr = devAddr;
    sRamAddr = ramAddr;
}

RECOMP_HOOK_RETURN("AudioLoad_SyncDma") void AudioLoad_afterSyncDma() {
    AudioApi_AfterSyncDma(sDevAddr, sRamAddr);
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
    AudioLoad_Dma(&gAudioCtx.currAudioFrameDmaIoMesgBuf[gAudioCtx.curAudioFrameDmaCount++], OS_MESG_PRI_NORMAL, OS_READ,
                  dmaDevAddr, dma->ramAddr, transfer, &gAudioCtx.curAudioFrameDmaQueue, medium, "SUPERDMA");
    *dmaIndexRef = dmaIndex;
    return (devAddr - dmaDevAddr) + dma->ramAddr;
}

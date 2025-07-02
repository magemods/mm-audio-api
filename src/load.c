#include "load.h"
#include "modding.h"
#include "recomputils.h"

RECOMP_DECLARE_EVENT(AudioApi_onInit());

extern DmaHandler sDmaHandler;
extern u8 gAudioHeap[0x138000];
extern void AudioLoad_InitSoundFont(s32 fontId);

/* -------------------------------------------------------------------------- */

AudioTable* AudioApi_CopyTable(AudioTable* table) {
    size_t count = table->header.numEntries;
    size_t size = sizeof(AudioTableHeader) + count * sizeof(AudioTableEntry);

    AudioTable* newTable = recomp_alloc(size);
    if (!newTable) return table;

    Lib_MemCpy(newTable, table, size);
    return newTable;
}

s32 AudioApi_AddTableEntry(AudioTable** tablePtr, AudioTableEntry* entry) {
    AudioTable* table = *tablePtr;
    size_t count = table->header.numEntries + 1;
    size_t size = sizeof(AudioTableHeader) + count * sizeof(AudioTableEntry);

    AudioTable* newTable = recomp_alloc(size);
    if (!newTable) return 0;

    Lib_MemCpy(newTable, table, size - sizeof(AudioTableEntry));
    recomp_free(table);

    newTable->entries[count - 1] = *entry;
    newTable->header.numEntries = count;

    *tablePtr = newTable;
    return count - 1;
}

void AudioApi_ReplaceTableEntry(AudioTable* table, AudioTableEntry* entry, s32 id) {
    if (id < table->header.numEntries) {
        table->entries[id] = *entry;
    }
}

void AudioApi_RestoreTableEntry(AudioTable* table, s32 id) {
    AudioTable* origTable = (AudioTable*)gSequenceTable;
    if (id < origTable->header.numEntries) {
        table->entries[id] = origTable->entries[id];
    }
}

RECOMP_PATCH void AudioLoad_InitTable(AudioTable* table, uintptr_t romAddr, u16 unkMediumParam) {
    s32 i;

    table->header.unkMediumParam = unkMediumParam;
    table->header.romAddr = romAddr;

    for (i = 0; i < table->header.numEntries; i++) {
        if ((table->entries[i].size != 0) && (table->entries[i].medium == MEDIUM_CART)) {
            // @mod don't relocate mod addrs
            if (!IS_KSEG0(table->entries[i].romAddr)) {
                table->entries[i].romAddr += romAddr;
            }
        }
    }
}

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

    // @mod Make copies of tables so we can resize later
    gAudioCtx.sequenceTable = AudioApi_CopyTable(gAudioCtx.sequenceTable);
    gAudioCtx.soundFontTable = AudioApi_CopyTable(gAudioCtx.soundFontTable);
    gAudioCtx.sampleBankTable = AudioApi_CopyTable(gAudioCtx.sampleBankTable);

    // @mod Call event for mods to register data
    AudioApi_onInit();

    gAudioCtx.numSequences = gAudioCtx.sequenceTable->header.numEntries;

    gAudioCtx.specId = 0;
    gAudioCtx.resetStatus = 1; // Set reset to immediately initialize the audio heap
    AudioHeap_ResetStep();

    // Initialize audio tables
    AudioLoad_InitTable(gAudioCtx.sequenceTable, SEGMENT_ROM_START(Audioseq), 0);
    AudioLoad_InitTable(gAudioCtx.soundFontTable, SEGMENT_ROM_START(Audiobank), 0);
    AudioLoad_InitTable(gAudioCtx.sampleBankTable, SEGMENT_ROM_START(Audiotable), 0);

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

void AudioApi_LoadSoundFont(u8* ramAddr, s32 fontId);
void AudioApi_LoadSequence(u8* ramAddr, s32 seqId);

static uintptr_t sDevAddr = 0;
static u8* sRamAddr = NULL;

RECOMP_HOOK("AudioLoad_SyncDma") void AudioLoad_onSyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium) {
    sDevAddr = devAddr;
    sRamAddr = ramAddr;
}

RECOMP_HOOK_RETURN("AudioLoad_SyncDma") void AudioLoad_afterSyncDma() {
    AudioTableEntry* entry;
    s32 id;

    for (id = 0; id < gAudioCtx.soundFontTable->header.numEntries; id++) {
        entry = &gAudioCtx.soundFontTable->entries[id];
        if (entry->romAddr == sDevAddr) {
            AudioApi_LoadSoundFont(sRamAddr, id);
            return;
        }
    }

    for (id = 0; id < gAudioCtx.sequenceTable->header.numEntries; id++) {
        entry = &gAudioCtx.sequenceTable->entries[id];
        if (entry->romAddr == sDevAddr) {
            AudioApi_LoadSequence(sRamAddr, id);
            return;
        }
    }
}

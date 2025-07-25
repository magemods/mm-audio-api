#include "init.h"
#include "modding.h"
#include "recomputils.h"
#include "heap.h"
#include "load.h"
#include "queue.h"

/**
 * This file is responsible for the main Audio API init process. It patches `AudioLoad_Init()`
 * in order to dispactch events at the correct time both internally and for client mods.
 *
 * Init status is tracked through `gAudioApiInitPhase`, which will signal to accept or queue commands.
 */

extern void AudioLoad_InitTable(AudioTable* table, uintptr_t romAddr, u16 unkMediumParam);
extern void AudioLoad_InitSoundFont(s32 fontId);

AudioApiInitPhase gAudioApiInitPhase = AUDIOAPI_INIT_NOT_READY;

// Internal queue events
RECOMP_DECLARE_EVENT(AudioApi_InitInternal());
RECOMP_DECLARE_EVENT(AudioApi_ReadyInternal());

// Public queue events
RECOMP_DECLARE_EVENT(AudioApi_Init());
RECOMP_DECLARE_EVENT(AudioApi_Ready());


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
    osCreateMesgQueue(&gAudioCtx.curAudioFrameDmaQueue, currAudioFrameDmaMesgBuf,
                      ARRAY_COUNT(currAudioFrameDmaMesgBuf));
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

    // @mod We only need to store the ai buffer here
    gAudioHeapInitSizes.initPoolSize = AIBUF_SIZE * ARRAY_COUNT(gAudioCtx.aiBuffers) + 0x100;

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
    gAudioCtx.soundFontList = recomp_alloc(numFonts * sizeof(SoundFont));

    for (i = 0; i < numFonts; i++) {
        AudioLoad_InitSoundFont(i);
    }

    gAudioCtxInitialized = true;
    osSendMesg(gAudioCtx.taskStartQueueP, (void*)gAudioCtx.totalTaskCount, OS_MESG_NOBLOCK);
}

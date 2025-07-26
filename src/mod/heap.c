#include "heap.h"
#include "modding.h"
#include "util.h"

/**
 * This file changes how the game's audio heap works allowing us to load larger audio data, as well
 * as permanently store this data in the extended mod memory. There is a total of 1.25 MB available.
 *
 * In the vanilla game, this is divided into permanently loaded data (sequence 0, soundfonts 0 & 1),
 * temporarily loaded data (current sequences and soundfonts), sample DMA buffers, sample caches,
 * and a few other misc pools.
 *
 * However, this imposes a limit on the number of sequences or soundfonts that can be loaded at one
 * time, and also does not leave us very much room for other data we need to move into this memory
 * such as adpcm book + loop data from custom soundfonts. This is especially true because the heap
 * is very fragmented into the different pools, and while there may be enough free space total,
 * there is no one place we can allocate enough memory.
 *
 * Instead, we combine the pools for permanent and temporary data into one load buffer that is used
 * to DMA sequence and soundfont data before it's copied into mod memory. We also combine the sample
 * DMA and cache into one pool that can be used for both.
 *
 * This still leaves us with around 0.5 MB free, which can be used at a later time or for expanding
 * the cache space.
 */

// The load buffer is where sequence and soundfonts will be loaded into before being moved into
// mod memory. It needs to be big enough to fit sequence 0, which is 0xC6B0, but sized a bit larger
// since we have plenty of space on the audio heap.
#define LOAD_BUFFER_SIZE 0x10000

// Since the RSP cannot read from mod memory, this is where sample chunks, adpcm book + loop data,
// and filters are written to for processing. It also acts as a cache that will be searched for
// overlapping ram addresses before writing duplicate entries. Cache entries are written in a round
// robin fashion.
//
// However, since audio is triple-buffered, we need to be careful when overwriting old cache entries
// since the RSP may not have processed commands using that memory range. The MIN_DISTANCE value
// defines how far away an entry must be from the current write position to be re-used. Empirically,
// one iteration of the audio loop uses around 0x5000, so we define a very safe min distance to
// ensure there are no audio glitches. The capacity value is the approximate number of entries it
// takes to fill the cache, preventing unnecessary cache searching.
#define RSP_CACHE_SIZE 0x80000
#define RSP_CACHE_MIN_DISTANCE 0x40000
#define RSP_CACHE_CAPACITY 2000

#define gTatumsPerBeat (gAudioTatumInit[1])

typedef struct LoadBufferEntry {
    u8* addr;
    size_t size;
    s16 tableType;
    s16 id;
    bool isFree;
} LoadBufferEntry;

typedef struct LoadBuffer {
    AudioAllocPool pool;
    LoadBufferEntry entries[16];
} LoadBuffer;

typedef struct RspCacheEntry {
    u8* cacheAddr;
    uintptr_t addr;
    size_t size;
    size_t offset;
} RspCacheEntry;

typedef struct RspCache {
    AudioAllocPool pool;
    RspCacheEntry entries[RSP_CACHE_CAPACITY];
    u32 pos;
} RspCache;

LoadBuffer loadBuffer;
RspCache rspCache;

extern void AudioHeap_InitSessionPool(AudioSessionPoolSplit* split);
extern void AudioHeap_ResetLoadStatus(void);
extern void* AudioHeap_AllocDmaMemoryZeroed(AudioAllocPool* pool, size_t size);
extern void AudioHeap_InitAdsrDecayTable(void);
extern void AudioHeap_InitReverb(s32 reverbIndex, ReverbSettings* settings, s32 isFirstInit);


void* AudioHeap_LoadBufferAlloc(s32 tableType, s32 id, size_t size) {
    AudioAllocPool* pool = &loadBuffer.pool;
    LoadBufferEntry* entry;

    if ((pool->curAddr + size) > (pool->startAddr + pool->size)) {
        return NULL;
    }

    entry = &loadBuffer.entries[pool->count];
    entry->addr = pool->curAddr;
    entry->size = size;
    entry->tableType = tableType;
    entry->id = id;
    entry->isFree = 0;

    pool->curAddr += ALIGN16(size);
    pool->count++;

    return entry->addr;
}

void AudioHeap_LoadBufferFree(s32 tableType, s32 id) {
    AudioAllocPool* pool = &loadBuffer.pool;
    LoadBufferEntry* entry;
    s32 i;

    for (i = 0; i < pool->count; i++) {
        entry = &loadBuffer.entries[i];
        if (entry->tableType == tableType && entry->id == id) {
            entry->isFree = 1;
        }
    }

    for (i = pool->count; i > 0; i--) {
        entry = &loadBuffer.entries[i - 1];
        if (!entry->isFree) {
            break;
        }
        pool->curAddr = entry->addr;
        pool->count--;
    }
}

bool AudioApi_RspCacheCheckDistance(RspCacheEntry* entry) {
    AudioAllocPool* pool = &rspCache.pool;

    u32 distance = ((uintptr_t)entry->cacheAddr < (uintptr_t)pool->curAddr)
        ? ((uintptr_t)entry->cacheAddr + pool->size - (uintptr_t)pool->curAddr)
        : ((uintptr_t)entry->cacheAddr - (uintptr_t)pool->curAddr);

    // Invalidate an entry if less than defined minimum distance so that it will not be
    // overwritten by the time the RSP processes the command
    if (distance < RSP_CACHE_MIN_DISTANCE) {
        entry->cacheAddr = NULL;
        return false;
    }
    return true;
}

void* AudioApi_RspCacheSearch(void* addr, size_t size) {
    AudioAllocPool* pool = &rspCache.pool;
    RspCacheEntry* entry;

    for (s32 i = 0; i < pool->count; i++) {
        entry = &rspCache.entries[i];
        if (entry->cacheAddr == NULL || !AudioApi_RspCacheCheckDistance(entry)) {
            continue;
        }
        if ((entry->addr <= (uintptr_t)addr) && ((uintptr_t)addr + size <= entry->addr + entry->size)) {
            return entry->cacheAddr + ((uintptr_t)addr - entry->addr);
        }
    }
    return NULL;
}

void* AudioApi_RspCacheOffsetSearch(void* addr, size_t size, size_t offset) {
    AudioAllocPool* pool = &rspCache.pool;
    RspCacheEntry* entry;

    for (s32 i = 0; i < pool->count; i++) {
        entry = &rspCache.entries[i];
        if (entry->cacheAddr == NULL || !AudioApi_RspCacheCheckDistance(entry)) {
            continue;
        }
        // Starting address must match exactly
        if (entry->addr != (uintptr_t)addr) {
            continue;
        }
        if ((entry->offset <= offset) && (offset + size <= entry->offset + entry->size)) {
            return entry->cacheAddr + (offset - entry->offset);
        }
    }
    return NULL;
}

void* AudioApi_RspCacheAlloc(void* addr, size_t size, size_t offset) {
    AudioAllocPool* pool = &rspCache.pool;
    RspCacheEntry* entry;
    u8* cacheAddr;

    // If not enough space at current pool address, loop back to start
    if ((pool->curAddr + size) > (pool->startAddr + pool->size)) {
        pool->curAddr = pool->startAddr;
    }

    entry = &rspCache.entries[rspCache.pos];
    entry->cacheAddr = pool->curAddr;
    entry->addr = (uintptr_t)addr;
    entry->size = size;
    entry->offset = offset;

    pool->curAddr += ALIGN16(size);
    pool->count = MIN(pool->count + 1, RSP_CACHE_CAPACITY);

    rspCache.pos = (rspCache.pos + 1) % RSP_CACHE_CAPACITY;

    return entry->cacheAddr;
}

void* AudioApi_RspCacheMemcpy(void* addr, size_t size) {
    void* cacheAddr;

    cacheAddr = AudioApi_RspCacheSearch(addr, size);
    if (cacheAddr != NULL) {
        return cacheAddr;
    }

    cacheAddr = AudioApi_RspCacheAlloc(addr, size, 0);
    Lib_MemCpy(cacheAddr, addr, size);

    return cacheAddr;
}

void AudioApi_RspCacheInvalidateLastEntry() {
    rspCache.pos = (rspCache.pos + RSP_CACHE_CAPACITY - 1) % RSP_CACHE_CAPACITY;
    rspCache.entries[rspCache.pos].cacheAddr = NULL;
}

void AudioApi_InitHeap() {
    AudioHeap_InitPool(&loadBuffer.pool,
                       AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, LOAD_BUFFER_SIZE), LOAD_BUFFER_SIZE);

    AudioHeap_InitPool(&rspCache.pool,
                       AudioHeap_AllocDmaMemory(&gAudioCtx.miscPool, RSP_CACHE_SIZE), RSP_CACHE_SIZE);

    loadBuffer.pool.startAddr = (void*)ALIGN16((uintptr_t)loadBuffer.pool.startAddr);
    rspCache.pool.startAddr = (void*)ALIGN16((uintptr_t)rspCache.pool.startAddr);
    rspCache.pos = 0;
}

RECOMP_PATCH void AudioHeap_Init(void) {
    size_t cachePoolSize;
    size_t miscPoolSize;
    u32 intMask;
    s32 reverbIndex;
    s32 i;
    AudioSpec* spec = &gAudioSpecs[gAudioCtx.specId]; // Audio Specifications

    gAudioCtx.sampleDmaCount = 0;

    // audio buffer parameters
    gAudioCtx.audioBufferParameters.samplingFreq = spec->samplingFreq;
    gAudioCtx.audioBufferParameters.aiSamplingFreq = osAiSetFrequency(gAudioCtx.audioBufferParameters.samplingFreq);

    gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget =
        ALIGN16(gAudioCtx.audioBufferParameters.samplingFreq / gAudioCtx.refreshRate);
    gAudioCtx.audioBufferParameters.numSamplesPerFrameMin =
        gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget - 0x10;
    gAudioCtx.audioBufferParameters.numSamplesPerFrameMax =
        gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget + 0x10;
    gAudioCtx.audioBufferParameters.updatesPerFrame =
        ((gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget + 0x10) / 0xD0) + 1;
    gAudioCtx.audioBufferParameters.numSamplesPerUpdate =
        (gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget / gAudioCtx.audioBufferParameters.updatesPerFrame) &
        ~7;
    gAudioCtx.audioBufferParameters.numSamplesPerUpdateMax = gAudioCtx.audioBufferParameters.numSamplesPerUpdate + 8;
    gAudioCtx.audioBufferParameters.numSamplesPerUpdateMin = gAudioCtx.audioBufferParameters.numSamplesPerUpdate - 8;
    gAudioCtx.audioBufferParameters.resampleRate = 32000.0f / (s32)gAudioCtx.audioBufferParameters.samplingFreq;
    gAudioCtx.audioBufferParameters.updatesPerFrameInvScaled =
        (1.0f / 256.0f) / gAudioCtx.audioBufferParameters.updatesPerFrame;
    gAudioCtx.audioBufferParameters.updatesPerFrameScaled = gAudioCtx.audioBufferParameters.updatesPerFrame / 4.0f;
    gAudioCtx.audioBufferParameters.updatesPerFrameInv = 1.0f / gAudioCtx.audioBufferParameters.updatesPerFrame;

    // sample dma size
    // gAudioCtx.sampleDmaBufSize1 = spec->sampleDmaBufSize1;
    // gAudioCtx.sampleDmaBufSize2 = spec->sampleDmaBufSize2;

    gAudioCtx.numNotes = spec->numNotes;
    gAudioCtx.audioBufferParameters.numSequencePlayers = spec->numSequencePlayers;

    if (gAudioCtx.audioBufferParameters.numSequencePlayers > 5) {
        gAudioCtx.audioBufferParameters.numSequencePlayers = 5;
    }

    gAudioCtx.numAbiCmdsMax = 8;
    gAudioCtx.unk_2 = spec->unk_14;
    gAudioCtx.maxTempo =
        (u32)(gAudioCtx.audioBufferParameters.updatesPerFrame * 2880000.0f / gTatumsPerBeat / gAudioCtx.unk_2960);

    gAudioCtx.unk_2870 = gAudioCtx.refreshRate;
    gAudioCtx.unk_2870 *= gAudioCtx.audioBufferParameters.updatesPerFrame;
    gAudioCtx.unk_2870 /= gAudioCtx.audioBufferParameters.aiSamplingFreq;
    gAudioCtx.unk_2870 /= gAudioCtx.maxTempo;

    gAudioCtx.audioBufferParameters.specUnk4 = spec->unk_04;
    gAudioCtx.audioBufferParameters.numSamplesPerFrameTarget *= gAudioCtx.audioBufferParameters.specUnk4;
    gAudioCtx.audioBufferParameters.numSamplesPerFrameMax *= gAudioCtx.audioBufferParameters.specUnk4;
    gAudioCtx.audioBufferParameters.numSamplesPerFrameMin *= gAudioCtx.audioBufferParameters.specUnk4;
    gAudioCtx.audioBufferParameters.updatesPerFrame *= gAudioCtx.audioBufferParameters.specUnk4;

    if (gAudioCtx.audioBufferParameters.specUnk4 >= 2) {
        gAudioCtx.audioBufferParameters.numSamplesPerFrameMax -= 0x10;
    }

    // Determine the maximum allowable number of audio command list entries for the rsp microcode
    gAudioCtx.maxAudioCmds =
        gAudioCtx.numNotes * 20 * gAudioCtx.audioBufferParameters.updatesPerFrame + spec->numReverbs * 30 + 800;

    cachePoolSize = 0;
    miscPoolSize = gAudioCtx.sessionPool.size - cachePoolSize - 0x100;

    // Session Pool Split (split into Cache and Misc heaps)
    gAudioCtx.sessionPoolSplit.miscPoolSize = miscPoolSize;
    gAudioCtx.sessionPoolSplit.cachePoolSize = cachePoolSize;
    AudioHeap_InitSessionPool(&gAudioCtx.sessionPoolSplit);

    // Initialize the custom load buffer and RSP cache
    AudioApi_InitHeap();

    AudioHeap_ResetLoadStatus();

    // Initialize notes
    gAudioCtx.notes = AudioHeap_AllocZeroed(&gAudioCtx.miscPool, gAudioCtx.numNotes * sizeof(Note));
    AudioPlayback_NoteInitAll();
    AudioList_InitNoteFreeList();

    gAudioCtx.sampleStateList =
        AudioHeap_AllocZeroed(&gAudioCtx.miscPool, gAudioCtx.audioBufferParameters.updatesPerFrame *
                                                       gAudioCtx.numNotes * sizeof(NoteSampleState));

    // Initialize audio binary interface command list buffer
    for (i = 0; i < ARRAY_COUNT(gAudioCtx.abiCmdBufs); i++) {
        gAudioCtx.abiCmdBufs[i] =
            AudioHeap_AllocDmaMemoryZeroed(&gAudioCtx.miscPool, gAudioCtx.maxAudioCmds * sizeof(Acmd));
    }

    // Initialize the decay rate table for ADSR
    gAudioCtx.adsrDecayTable = AudioHeap_Alloc(&gAudioCtx.miscPool, 0x100 * sizeof(f32));
    AudioHeap_InitAdsrDecayTable();

    // Initialize reverbs
    for (reverbIndex = 0; reverbIndex < ARRAY_COUNT(gAudioCtx.synthesisReverbs); reverbIndex++) {
        gAudioCtx.synthesisReverbs[reverbIndex].useReverb = 0;
    }

    gAudioCtx.numSynthesisReverbs = spec->numReverbs;
    for (reverbIndex = 0; reverbIndex < gAudioCtx.numSynthesisReverbs; reverbIndex++) {
        AudioHeap_InitReverb(reverbIndex, &spec->reverbSettings[reverbIndex], true);
    }

    // Initialize sequence players
    AudioScript_InitSequencePlayers();
    for (i = 0; i < gAudioCtx.audioBufferParameters.numSequencePlayers; i++) {
        AudioScript_InitSequencePlayerChannels(i);
        AudioScript_ResetSequencePlayer(&gAudioCtx.seqPlayers[i]);
    }

    // Initialize two additional caches on the audio heap to store individual audio samples
    // AudioHeap_InitSampleCaches(spec->persistentSampleCacheSize, spec->temporarySampleCacheSize);
    // AudioLoad_InitSampleDmaBuffers(gAudioCtx.numNotes);

    // Initialize Loads
    gAudioCtx.preloadSampleStackTop = 0;
    AudioLoad_InitSlowLoads();
    AudioLoad_InitScriptLoads();
    AudioLoad_InitAsyncLoads();
    gAudioCtx.unk_4 = 0x1000;
    // AudioLoad_LoadPermanentSamples();

    intMask = osSetIntMask(1);
    osWritebackDCacheAll();
    osSetIntMask(intMask);
}

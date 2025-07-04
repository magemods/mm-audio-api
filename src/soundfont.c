#include "global.h"
#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"

RECOMP_DECLARE_EVENT(AudioApi_onLoadSoundFont(u8* ramAddr, s32 fontId));

SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src);
Instrument* AudioApi_CopyInstrument(Instrument* src);
Sample* AudioApi_CopySample(Sample* src);

void AudioApi_FreeSoundEffect(SoundEffect* sfx);
void AudioApi_FreeInstrument(Instrument* instrument);
void AudioApi_FreeSample(Sample* sample);

typedef struct {
    s32 id;
    void* value;
} SoundFontMapEntry;

U32MemoryHashmapHandle drumMap;
U32MemoryHashmapHandle sfxMap;
U32MemoryHashmapHandle instrumentMap;

RECOMP_CALLBACK("*", recomp_on_init) void AudioApi_SoundFontInit() {
    drumMap = recomputil_create_u32_memory_hashmap(sizeof(SoundFontMapEntry));
    sfxMap = recomputil_create_u32_memory_hashmap(sizeof(SoundFontMapEntry));
    instrumentMap = recomputil_create_u32_memory_hashmap(sizeof(SoundFontMapEntry));
}

RECOMP_EXPORT int AudioApi_ReplaceSoundEffect(SoundEffect* sfx, s32 sfxId) {
    SoundEffect* copy = AudioApi_CopySoundEffect(sfx);
    if (!copy) return 0;

    SoundFontMapEntry entry = { sfxId, copy };

    u32 count = recomputil_u32_memory_hashmap_size(sfxMap);
    if (!recomputil_u32_memory_hashmap_create(sfxMap, count)) {
        AudioApi_FreeSoundEffect(copy);
        return 0;
    }

    SoundFontMapEntry* entryAddr = recomputil_u32_memory_hashmap_get(sfxMap, count);
    if (!entryAddr) {
        AudioApi_FreeSoundEffect(copy);
        return 0;
    }

    *entryAddr = entry;
    return 1;
}

RECOMP_EXPORT int AudioApi_ReplaceInstrument(Instrument* instrument, s32 instId) {
    Instrument* copy = AudioApi_CopyInstrument(instrument);
    if (!copy) return 0;

    SoundFontMapEntry entry = { instId, copy };

    u32 count = recomputil_u32_memory_hashmap_size(instrumentMap);
    if (!recomputil_u32_memory_hashmap_create(instrumentMap, count)) {
        AudioApi_FreeInstrument(copy);
        return 0;
    }

    SoundFontMapEntry* entryAddr = recomputil_u32_memory_hashmap_get(instrumentMap, count);
    if (!entryAddr) {
        AudioApi_FreeInstrument(copy);
        return 0;
    }

    *entryAddr = entry;
    return 1;
}

void AudioApi_ApplySoundFont0Changes(u8* ramAddr) {
    uintptr_t* fontData = (uintptr_t*)ramAddr;
    u32 i;

    // The first u32 in fontData is an offset to a list of offsets to the drums
    // The second u32 in fontData is an offset to the first sound effect entry
    // Starting from the 3rd u32 in fontData is the list of offsets to the instruments

    for (i = 0; i < recomputil_u32_memory_hashmap_size(sfxMap); i++) {
        SoundFontMapEntry* entry = recomputil_u32_memory_hashmap_get(sfxMap, i);
        if (!entry) continue;

        SoundEffect* sfx = (SoundEffect*)(ramAddr + fontData[1]) + entry->id;
        *sfx = *(SoundEffect*)entry->value;
    }


    for (i = 0; i < recomputil_u32_memory_hashmap_size(instrumentMap); i++) {
        SoundFontMapEntry* entry = recomputil_u32_memory_hashmap_get(instrumentMap, i);
        if (!entry) continue;

        Instrument* instrument = (Instrument*)(ramAddr + fontData[2 + entry->id]);
        *instrument = *(Instrument*)entry->value;
    }
}

RECOMP_CALLBACK(".", AudioApi_afterSyncDma) void AudioApi_DispatchSoundFontEvent(uintptr_t devAddr, u8* ramAddr) {
    AudioTableEntry* entry;
    s32 fontId;

    for (fontId = 0; fontId < gAudioCtx.soundFontTable->header.numEntries; fontId++) {
        entry = &gAudioCtx.soundFontTable->entries[fontId];
        if (entry->romAddr == devAddr) {
            if (fontId == 0) {
                AudioApi_ApplySoundFont0Changes(ramAddr);
            }
            AudioApi_onLoadSoundFont(ramAddr, fontId);
            return;
        }
    }
}

SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src) {
    if (!src) return NULL;

    SoundEffect* copy = recomp_alloc(sizeof(SoundEffect));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(SoundEffect));

    if (src->tunedSample.sample) {
        copy->tunedSample.sample = AudioApi_CopySample(src->tunedSample.sample);
        if (!copy->tunedSample.sample) {
            AudioApi_FreeSoundEffect(copy);
            return NULL;
        }
    }

    return copy;
}

Instrument* AudioApi_CopyInstrument(Instrument* src) {
    if (!src) return NULL;

    Instrument* copy = recomp_alloc(sizeof(Instrument));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Instrument));
    copy->isRelocated = 1;

    if (src->envelope) {
        size_t envCount = 0;
        while (src->envelope[envCount].delay != ADSR_HANG) {
            envCount++;
        }
        envCount++;

        copy->envelope = recomp_alloc(sizeof(EnvelopePoint) * envCount);
        if (!copy->envelope) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
        Lib_MemCpy(copy->envelope, src->envelope, sizeof(EnvelopePoint) * envCount);
    }

    if (src->lowPitchTunedSample.sample) {
        copy->lowPitchTunedSample.sample = AudioApi_CopySample(src->lowPitchTunedSample.sample);
        if (!copy->lowPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    if (src->normalPitchTunedSample.sample) {
        copy->normalPitchTunedSample.sample = AudioApi_CopySample(src->normalPitchTunedSample.sample);
        if (!copy->normalPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    if (src->highPitchTunedSample.sample) {
        copy->highPitchTunedSample.sample = AudioApi_CopySample(src->highPitchTunedSample.sample);
        if (!copy->highPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    return copy;
}

Sample* AudioApi_CopySample(Sample* src) {
    if (!src) return NULL;

    Sample* copy = recomp_alloc(sizeof(Sample));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Sample));
    copy->medium = MEDIUM_CART;
    copy->isRelocated = 1;

    if (src->loop) {
        // s16 predictorState[16] only exists if count != 0.
        size_t loopSize = (src->loop->header.count != 0) ? sizeof(AdpcmLoop) : sizeof(AdpcmLoopHeader);

        copy->loop = recomp_alloc(loopSize);
        if (!copy->loop) {
            AudioApi_FreeSample(copy);
            return NULL;
        }
        Lib_MemCpy(copy->loop, src->loop, loopSize);
    }

    if (src->book) {
        s32 order = src->book->header.order;
        s32 numPredictors = src->book->header.numPredictors;
        size_t bookSize = sizeof(AdpcmBookHeader) + sizeof(s16) * 8 * order * numPredictors;

        copy->book = recomp_alloc(bookSize);
        if (!copy->book) {
            AudioApi_FreeSample(copy);
            return NULL;
        }
        Lib_MemCpy(copy->book, src->book, bookSize);
    }

    return copy;
}

void AudioApi_FreeSoundEffect(SoundEffect* sfx) {
    if (!sfx) return;
    if (sfx->tunedSample.sample) AudioApi_FreeSample(sfx->tunedSample.sample);
    recomp_free(sfx);
}

void AudioApi_FreeInstrument(Instrument* instrument) {
    if (!instrument) return;
    if (instrument->envelope) recomp_free(instrument->envelope);
    if (instrument->lowPitchTunedSample.sample) AudioApi_FreeSample(instrument->lowPitchTunedSample.sample);
    if (instrument->normalPitchTunedSample.sample) AudioApi_FreeSample(instrument->normalPitchTunedSample.sample);
    if (instrument->highPitchTunedSample.sample) AudioApi_FreeSample(instrument->highPitchTunedSample.sample);
    recomp_free(instrument);
}

void AudioApi_FreeSample(Sample* sample) {
    if (!sample) return;
    if (sample->loop) recomp_free(sample->loop);
    if (sample->book) recomp_free(sample->book);
    recomp_free(sample);
}

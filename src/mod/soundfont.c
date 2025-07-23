#include "soundfont.h"
#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "recompdata.h"
#include "init.h"
#include "heap.h"
#include "queue.h"
#include "util.h"

/**
 * This file provides the public API for creating and modifying soundfont data
 *
 * Since the soundfonts are only read from the ROM when needed, we need to queue actions until the
 * font is actually loaded. Once loaded, we will make a copy of the ROM data into our CustomSoundFont
 * type and update the romAddr in the soundFont table.
 *
 */

#define NA_SOUNDFONT_MAX 0x28 // Number of vanilla soundfonts
#define SOUNDFONT_MAX_INSTRUMENTS 126
#define SOUNDFONT_MAX_DRUMS 2048
#define SOUNDFONT_MAX_SFX 2048
#define SOUNDFONT_DEFAULT_INSTRUMENT_CAPACITY 32
#define SOUNDFONT_DEFAULT_DRUM_CAPACITY 32
#define SOUNDFONT_DEFAULT_SFX_CAPACITY 8

// Relocate an offset (relative to the start of the font data) to a pointer (a ram address)
#define RELOC_TO_RAM(x, base) (void*)((uintptr_t)(x) + (uintptr_t)(IS_KSEG0(x) ? 0 : base))

typedef struct {
    s32 sampleBankId1;
    s32 sampleBankId2;
    uintptr_t baseAddr1;
    uintptr_t baseAddr2;
    u32 medium1;
    u32 medium2;
} SampleBankRelocInfo;

typedef enum {
    AUDIOAPI_CMD_OP_REPLACE_SOUNDFONT,
    AUDIOAPI_CMD_OP_ADD_DRUM,
    AUDIOAPI_CMD_OP_REPLACE_DRUM,
    AUDIOAPI_CMD_OP_ADD_SOUNDEFFECT,
    AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT,
    AUDIOAPI_CMD_OP_ADD_INSTRUMENT,
    AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT,
} AudioApiSoundFontQueueOp;

RecompQueue* soundFontInitQueue;
RecompQueue* soundFontLoadQueue;
U32ValueHashmapHandle sampleHashmap;
u16 soundFontTableCapacity = NA_SOUNDFONT_MAX;

void AudioApi_SoundFontQueueDrain(RecompQueueCmd* cmd);
void AudioLoad_RelocateSample(TunedSample* tunedSample, void* fontData, SampleBankRelocInfo* sampleBankReloc);
bool AudioApi_GrowSoundFontTable();
bool AudioApi_GrowInstrumentList(CustomSoundFont** soundFontPtr);
bool AudioApi_GrowDrumList(CustomSoundFont* soundFont);
bool AudioApi_GrowSoundEffectList(CustomSoundFont* soundFont);
Drum* AudioApi_CopyDrum(Drum* src);
SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src);
Instrument* AudioApi_CopyInstrument(Instrument* src);
Sample* AudioApi_CopySample(Sample* src);
void AudioApi_FreeDrum(Drum* drum);
void AudioApi_FreeSoundEffect(SoundEffect* sfx);
void AudioApi_FreeInstrument(Instrument* instrument);
void AudioApi_FreeSample(Sample* sample);

extern u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);

RECOMP_DECLARE_EVENT(AudioApi_SoundFontLoaded(s32 fontId, u8* ramAddr));


RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_SoundFontInit() {
    // Queue for the init phase so that mods can register data in the correct order
    soundFontInitQueue = RecompQueue_Create();
    // Queue for when ROM soundfonts are actually loaded in order to apply our changes
    soundFontLoadQueue = RecompQueue_Create();
    // Hashmap to detect duplicate sample structs before copy
    sampleHashmap = recomputil_create_u32_value_hashmap();
}

RECOMP_CALLBACK(".", AudioApi_ReadyInternal) void AudioApi_SoundFontReady() {
    RecompQueue_Drain(soundFontInitQueue, AudioApi_SoundFontQueueDrain);
    RecompQueue_Destroy(soundFontInitQueue);
}

RECOMP_EXPORT s32 AudioApi_AddSoundFont(AudioTableEntry* entry) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    // Find the next available font ID
    s32 newFontId = gAudioCtx.soundFontTable->header.numEntries;
    if (newFontId >= soundFontTableCapacity) {
        if (!AudioApi_GrowSoundFontTable()) {
            return -1;
        }
    }
    gAudioCtx.soundFontTable->header.numEntries = newFontId + 1;
    gAudioCtx.soundFontTable->entries[newFontId] = *entry;
    return newFontId;
}

RECOMP_EXPORT void AudioApi_ReplaceSoundFont(s32 fontId, AudioTableEntry* entry) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioTableEntry* copy = recomp_alloc(sizeof(AudioTableEntry));
        if (!copy) {
            return;
        }
        *copy = *entry;
        RecompQueue_PushIfNotQueued(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_SOUNDFONT,
                                    fontId, 0, (void**)&copy);
        return;
    }
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return;
    }
    gAudioCtx.soundFontTable->entries[fontId] = *entry;
}

RECOMP_EXPORT void AudioApi_RestoreSoundFont(s32 fontId) {
    if (gAudioApiInitPhase < AUDIOAPI_INIT_READY) {
        return;
    }
    if (fontId >= gSoundFontTable.header.numEntries) {
        return;
    }
    gAudioCtx.soundFontTable->entries[fontId] = gSoundFontTable.entries[fontId];
}

RECOMP_EXPORT s32 AudioApi_CreateEmptySoundFont() {
    CustomSoundFont* soundFont;
    size_t size;

    size = sizeof(CustomSoundFont) + sizeof(uintptr_t) * SOUNDFONT_DEFAULT_INSTRUMENT_CAPACITY;
    soundFont = recomp_alloc(size);
    if (!soundFont) goto cleanup;
    Lib_MemSet(soundFont, 0, size);

    soundFont->numInstruments = 0;
    soundFont->numDrums = 0;
    soundFont->numSfx = 0;
    soundFont->instrumentsCapacity = SOUNDFONT_DEFAULT_INSTRUMENT_CAPACITY;
    soundFont->drumsCapacity = SOUNDFONT_DEFAULT_DRUM_CAPACITY;
    soundFont->sfxCapacity = SOUNDFONT_DEFAULT_SFX_CAPACITY;

    size = soundFont->drumsCapacity * sizeof(uintptr_t);
    soundFont->drums = recomp_alloc(size);
    if (!soundFont->drums) goto cleanup;
    Lib_MemSet(soundFont->drums, 0, size);

    size = soundFont->sfxCapacity * sizeof(SoundEffect);
    soundFont->soundEffects = recomp_alloc(size);
    if (!soundFont->soundEffects) goto cleanup;
    Lib_MemSet(soundFont->soundEffects, 0, size);

    u8 sampleBank1 = 1;
    u8 sampleBank2 = 255;

    AudioTableEntry entry = {
        (uintptr_t) soundFont,
        sizeof(CustomSoundFont),
        MEDIUM_CART,
        CACHE_EITHER,
        (sampleBank1 << 8) | sampleBank2,
        0, 0,
    };

    s32 fontId = AudioApi_AddSoundFont(&entry);
    if (fontId == -1) goto cleanup;

    return fontId;

 cleanup:
    if (IS_RECOMP_ALLOC(soundFont->drums)) {
        recomp_free(soundFont->drums);
    }
    if (IS_RECOMP_ALLOC(soundFont->soundEffects)) {
        recomp_free(soundFont->drums);
    }
    if (IS_RECOMP_ALLOC(soundFont)) {
        recomp_free(soundFont);
    }
    return -1;
}

CustomSoundFont* AudioApi_ImportVanillaSoundFont(s32 fontId, uintptr_t* fontData) {
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return NULL;
    }
    CustomSoundFont* soundFont;
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];
    u32 numInstruments = gAudioCtx.soundFontList[fontId].numInstruments;
    u32 numDrums = gAudioCtx.soundFontList[fontId].numDrums;
    u32 numSfx = gAudioCtx.soundFontList[fontId].numSfx;
    size_t size, capacity;

    capacity = SOUNDFONT_DEFAULT_INSTRUMENT_CAPACITY;
    while (numInstruments > capacity) capacity <<= 1;
    size = sizeof(CustomSoundFont) + sizeof(uintptr_t) * capacity;
    soundFont = recomp_alloc(size);
    if (!soundFont) goto cleanup;
    Lib_MemSet(soundFont, 0, size);
    Lib_MemCpy((void*)soundFont->instruments, fontData + 2,
               numInstruments * sizeof(uintptr_t));

    soundFont->numInstruments = numInstruments;
    soundFont->numDrums = numDrums;
    soundFont->numSfx = numSfx;
    soundFont->instrumentsCapacity = capacity;
    soundFont->drumsCapacity = SOUNDFONT_DEFAULT_DRUM_CAPACITY;
    soundFont->sfxCapacity = SOUNDFONT_DEFAULT_SFX_CAPACITY;

    while (numDrums > soundFont->drumsCapacity) soundFont->drumsCapacity <<= 1;
    size = soundFont->drumsCapacity * sizeof(uintptr_t);
    soundFont->drums = recomp_alloc(size);
    if (!soundFont->drums) goto cleanup;
    Lib_MemSet(soundFont->drums, 0, size);
    Lib_MemCpy(soundFont->drums, (void*)RELOC_TO_RAM(fontData[0], fontData),
               numDrums * sizeof(uintptr_t));

    while (numSfx > soundFont->sfxCapacity) soundFont->sfxCapacity <<= 1;
    size = soundFont->sfxCapacity * sizeof(SoundEffect);
    soundFont->soundEffects = recomp_alloc(size);
    if (!soundFont->soundEffects) goto cleanup;
    Lib_MemSet(soundFont->soundEffects, 0, size);
    Lib_MemCpy(soundFont->soundEffects, (void*)RELOC_TO_RAM(fontData[1], fontData),
               numSfx * sizeof(SoundEffect));

    soundFont->origRomAddr = entry->romAddr;
    entry->romAddr = (uintptr_t)soundFont;

    return soundFont;

 cleanup:
    if (IS_RECOMP_ALLOC(soundFont->drums)) {
        recomp_free(soundFont->drums);
    }
    if (IS_RECOMP_ALLOC(soundFont->soundEffects)) {
        recomp_free(soundFont->drums);
    }
    if (IS_RECOMP_ALLOC(soundFont)) {
        recomp_free(soundFont);
    }
    return NULL;
}

s32 AudioApi_AddInstrumentInternal(CustomSoundFont* soundFont, Instrument* instrument) {
    if (soundFont->numInstruments >= soundFont->instrumentsCapacity) {
        if (!AudioApi_GrowInstrumentList(&soundFont)) {
            return -1;
        }
    }
    s32 instId = soundFont->numInstruments++;
    soundFont->instruments[instId] = instrument;
    return instId;
}

RECOMP_EXPORT s32 AudioApi_AddInstrument(s32 fontId, Instrument* instrument) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return -1;
    }

    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];
    s32 instId = (entry->shortData2 >> 8) & 0xFF;
    if (instId >= SOUNDFONT_MAX_INSTRUMENTS) {
        return -1;
    }

    Instrument* copy = AudioApi_CopyInstrument(instrument);
    if (!copy) {
        return -1;
    }

    if (IS_KSEG0(entry->romAddr)) {
        instId = AudioApi_AddInstrumentInternal((CustomSoundFont*)entry->romAddr, copy);
    } else {
        RecompQueue_Push(soundFontLoadQueue, AUDIOAPI_CMD_OP_ADD_INSTRUMENT, fontId, instId, (void**)&copy);
    }
    if (instId == -1) {
        AudioApi_FreeInstrument(copy);
        return -1;
    }

    entry->size += sizeof(uintptr_t);
    entry->shortData2 = ((instId + 1 & 0xFF) << 8) | (entry->shortData2 & 0xFF);
    if (gAudioCtx.soundFontList) {
        gAudioCtx.soundFontList[fontId].numInstruments = instId + 1;
    }
    return instId;
}

s32 AudioApi_AddDrumInternal(CustomSoundFont* soundFont, Drum* drum) {
    if (soundFont->numDrums >= soundFont->drumsCapacity) {
        if (!AudioApi_GrowDrumList(soundFont)) {
            return -1;
        }
    }
    s32 drumId = soundFont->numDrums++;
    soundFont->drums[drumId] = drum;
    return drumId;
}

RECOMP_EXPORT s32 AudioApi_AddDrum(s32 fontId, Drum* drum) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return -1;
    }

    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];
    s32 drumId = entry->shortData2 & 0xFF;
    if (drumId >= SOUNDFONT_MAX_DRUMS) {
        return -1;
    }

    Drum* copy = AudioApi_CopyDrum(drum);
    if (!copy) {
        return -1;
    }

    if (IS_KSEG0(entry->romAddr)) {
        drumId = AudioApi_AddDrumInternal((CustomSoundFont*)entry->romAddr, copy);
    } else {
        RecompQueue_Push(soundFontLoadQueue, AUDIOAPI_CMD_OP_ADD_DRUM, fontId, drumId, (void**)&copy);
    }
    if (drumId == -1) {
        AudioApi_FreeDrum(copy);
        return -1;
    }

    entry->shortData2 = (entry->shortData2 & 0xFF00) | (drumId + 1);
    if (gAudioCtx.soundFontList) {
        gAudioCtx.soundFontList[fontId].numDrums = drumId + 1;
    }
    return drumId;
}

s32 AudioApi_AddSoundEffectInternal(CustomSoundFont* soundFont, SoundEffect* sfx) {
    if (soundFont->numSfx >= soundFont->sfxCapacity) {
        if (!AudioApi_GrowSoundEffectList(soundFont)) {
            return -1;
        }
    }
    s32 sfxId = soundFont->numSfx++;
    soundFont->soundEffects[sfxId] = *sfx;
    recomp_free(sfx);
    return sfxId;
}

RECOMP_EXPORT s32 AudioApi_AddSoundEffect(s32 fontId, SoundEffect* sfx) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return -1;
    }
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return -1;
    }

    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];
    s32 sfxId = entry->shortData3;
    if (sfxId >= SOUNDFONT_MAX_SFX) {
        return -1;
    }

    SoundEffect* copy = AudioApi_CopySoundEffect(sfx);
    if (!copy) {
        return -1;
    }

    if (IS_KSEG0(entry->romAddr)) {
        sfxId = AudioApi_AddSoundEffectInternal((CustomSoundFont*)entry->romAddr, copy);
    } else {
        RecompQueue_Push(soundFontLoadQueue, AUDIOAPI_CMD_OP_ADD_SOUNDEFFECT, fontId, sfxId, (void**)&copy);
    }
    if (sfxId == -1) {
        AudioApi_FreeSoundEffect(copy);
        return -1;
    }

    entry->shortData3 = sfxId + 1;
    if (gAudioCtx.soundFontList) {
        gAudioCtx.soundFontList[fontId].numSfx = sfxId + 1;
    }
    return sfxId;
}

void AudioApi_ReplaceDrumInternal(CustomSoundFont* soundFont, s32 drumId, Drum* drum) {
    if (drumId < soundFont->numDrums) {
        soundFont->drums[drumId] = drum;
    }
}

RECOMP_EXPORT void AudioApi_ReplaceDrum(s32 fontId, s32 drumId, Drum* drum) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    Drum* copy = AudioApi_CopyDrum(drum);
    if (!copy) return;

    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        RecompQueue_PushIfNotQueued(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_DRUM,
                                    fontId, drumId, (void**)&copy);
        return;
    }

    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return;
    }
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];

    if (IS_KSEG0(entry->romAddr)) {
        AudioApi_ReplaceDrumInternal((CustomSoundFont*)entry->romAddr, drumId, copy);
    } else {
        RecompQueue_PushIfNotQueued(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_DRUM,
                                    fontId, drumId, (void**)&copy);
    }
}

void AudioApi_ReplaceSoundEffectInternal(CustomSoundFont* soundFont, s32 sfxId, SoundEffect* sfx) {
    if (sfxId < soundFont->numSfx) {
        soundFont->soundEffects[sfxId] = *sfx;
    }
    recomp_free(sfx);
}

RECOMP_EXPORT void AudioApi_ReplaceSoundEffect(s32 fontId, s32 sfxId, SoundEffect* sfx) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    SoundEffect* copy = AudioApi_CopySoundEffect(sfx);
    if (!copy) return;

    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        RecompQueue_PushIfNotQueued(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT,
                                    fontId, sfxId, (void**)&copy);
        return;
    }

    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return;
    }
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];

    if (IS_KSEG0(entry->romAddr)) {
        AudioApi_ReplaceSoundEffectInternal((CustomSoundFont*)entry->romAddr, sfxId, copy);
    } else {
        RecompQueue_PushIfNotQueued(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT,
                                    fontId, sfxId, (void**)&copy);
    }
}

void AudioApi_ReplaceInstrumentInternal(CustomSoundFont* soundFont, s32 instId, Instrument* instrument) {
    if (instId < soundFont->numInstruments)  {
        soundFont->instruments[instId] = instrument;
    }
}

RECOMP_EXPORT void AudioApi_ReplaceInstrument(s32 fontId, s32 instId, Instrument* instrument) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    Instrument* copy = AudioApi_CopyInstrument(instrument);
    if (!copy) return;

    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        RecompQueue_PushIfNotQueued(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT,
                                    fontId, instId, (void**)&copy);
        return;
    }

    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return;
    }
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];

    if (IS_KSEG0(entry->romAddr)) {
        AudioApi_ReplaceInstrumentInternal((CustomSoundFont*)entry->romAddr, instId, copy);
    } else {
        RecompQueue_PushIfNotQueued(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT,
                                    fontId, instId, (void**)&copy);
    }
}

void AudioApi_SoundFontQueueDrain(RecompQueueCmd* cmd) {
    s32 fontId = cmd->arg0;
    if (fontId >= gAudioCtx.soundFontTable->header.numEntries) {
        return;
    }
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];

    if (cmd->op == AUDIOAPI_CMD_OP_REPLACE_SOUNDFONT) {
        AudioApi_ReplaceSoundFont(cmd->arg0, (AudioTableEntry*) cmd->asPtr);
        recomp_free(cmd->asPtr);
        return;
    }

    if (IS_KSEG0(entry->romAddr)) {
        // If the font is in memory (e.g. a CustomSoundFont), we can call the replace command now
        CustomSoundFont* soundFont = (CustomSoundFont*)entry->romAddr;
        switch (cmd->op) {
        case AUDIOAPI_CMD_OP_REPLACE_DRUM:
            AudioApi_ReplaceDrumInternal(soundFont, cmd->arg1, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT:
            AudioApi_ReplaceSoundEffectInternal(soundFont, cmd->arg1, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT:
            AudioApi_ReplaceInstrumentInternal(soundFont, cmd->arg1, cmd->asPtr);
            break;
        }
    } else {
        // Otherwise we need to move it to load queue to apply once the font is loaded
        switch (cmd->op) {
        case AUDIOAPI_CMD_OP_ADD_DRUM:
        case AUDIOAPI_CMD_OP_REPLACE_DRUM:
        case AUDIOAPI_CMD_OP_ADD_SOUNDEFFECT:
        case AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT:
        case AUDIOAPI_CMD_OP_ADD_INSTRUMENT:
        case AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT:
            RecompQueue_PushIfNotQueued(soundFontLoadQueue, cmd->op, cmd->arg0, cmd->arg1, &cmd->data);
            break;
        }
    }
}

void AudioApi_ApplySoundFontChanges(s32 fontId, CustomSoundFont* customSoundFont) {
    RecompQueueCmd* cmd;
    for (s32 i = 0; i < soundFontLoadQueue->numEntries; i++) {
        cmd = &soundFontLoadQueue->entries[i];
        if (cmd->arg0 != (u32)fontId) {
            continue;
        }
        switch (cmd->op) {
        case AUDIOAPI_CMD_OP_ADD_DRUM:
            AudioApi_AddDrumInternal(customSoundFont, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_DRUM:
            AudioApi_ReplaceDrumInternal(customSoundFont, cmd->arg1, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_ADD_SOUNDEFFECT:
            AudioApi_AddSoundEffectInternal(customSoundFont, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT:
            AudioApi_ReplaceSoundEffectInternal(customSoundFont, cmd->arg1, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_ADD_INSTRUMENT:
            AudioApi_AddInstrumentInternal(customSoundFont, cmd->asPtr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT:
            AudioApi_ReplaceInstrumentInternal(customSoundFont, cmd->arg1, cmd->asPtr);
            break;
        }
    }
}

/**
 * Read and extract information from soundFont binary loaded into ram.
 * Also relocate offsets into pointers within this loaded soundFont
 */
RECOMP_PATCH void AudioLoad_RelocateFont(s32 fontId, void* fontDataStartAddr, SampleBankRelocInfo* sampleBankReloc) {
    CustomSoundFont* fontData = (CustomSoundFont*)fontDataStartAddr;
    AudioTableEntry* entry = &gAudioCtx.soundFontTable->entries[fontId];
    Instrument* inst;
    Drum* drum;
    SoundEffect* soundEffect;
    s32 i;

    // We've just loaded this font from the ROM, so apply any changes from our load queue
    if (IS_AUDIO_HEAP_MEMORY(fontData)) {
        fontData = AudioApi_ImportVanillaSoundFont(fontId, (uintptr_t*)fontDataStartAddr);
        AudioApi_ApplySoundFontChanges(fontId, fontData);
    }

    for (i = 0; i < fontData->numDrums; i++) {
        if (fontData->drums[i] == NULL) {
            continue;
        }

        drum = RELOC_TO_RAM(fontData->drums[i], fontDataStartAddr);
        drum->envelope = RELOC_TO_RAM(drum->envelope, fontDataStartAddr);
        drum->isRelocated = true;

        AudioLoad_RelocateSample(&drum->tunedSample, fontDataStartAddr, sampleBankReloc);

        if (IS_AUDIO_HEAP_MEMORY(drum)) {
            fontData->drums[i] = AudioApi_CopyDrum(drum);
        }
    }

    for (i = 0; i < fontData->numSfx; i++) {
        soundEffect = &fontData->soundEffects[i];
        if (soundEffect == NULL || soundEffect->tunedSample.sample == NULL) {
            continue;
        }

        AudioLoad_RelocateSample(&soundEffect->tunedSample, fontDataStartAddr, sampleBankReloc);

        if (IS_AUDIO_HEAP_MEMORY(soundEffect->tunedSample.sample)) {
            soundEffect->tunedSample.sample = AudioApi_CopySample(soundEffect->tunedSample.sample);
        }
    }

    for (i = 0; i < fontData->numInstruments; i++) {
        if (fontData->instruments[i] == NULL) {
            continue;
        }

        inst = RELOC_TO_RAM(fontData->instruments[i], fontDataStartAddr);
        inst->envelope = RELOC_TO_RAM(inst->envelope, fontDataStartAddr);
        inst->isRelocated = true;

        AudioLoad_RelocateSample(&inst->normalPitchTunedSample, fontDataStartAddr, sampleBankReloc);

        if (inst->normalRangeLo != 0) {
            AudioLoad_RelocateSample(&inst->lowPitchTunedSample, fontDataStartAddr, sampleBankReloc);
        }
        if (inst->normalRangeHi != 0x7F) {
            AudioLoad_RelocateSample(&inst->highPitchTunedSample, fontDataStartAddr, sampleBankReloc);
        }

        if (IS_AUDIO_HEAP_MEMORY(inst)) {
            fontData->instruments[i] = AudioApi_CopyInstrument(inst);
        }
    }

    // Update counts after applying any queued changes
    gAudioCtx.soundFontList[fontId].numInstruments = fontData->numInstruments;
    gAudioCtx.soundFontList[fontId].numDrums = fontData->numDrums;
    gAudioCtx.soundFontList[fontId].numSfx = fontData->numSfx;

    // Store the relocated pointers
    gAudioCtx.soundFontList[fontId].drums = fontData->drums;
    gAudioCtx.soundFontList[fontId].soundEffects = fontData->soundEffects;
    gAudioCtx.soundFontList[fontId].instruments = fontData->instruments;

    // Dispatch loaded event
    AudioApi_SoundFontLoaded(fontId, (u8*)fontData);
}

RECOMP_PATCH void AudioLoad_RelocateSample(TunedSample* tunedSample, void* fontData, SampleBankRelocInfo* sampleBankReloc) {
    Sample* sample = tunedSample->sample = RELOC_TO_RAM(tunedSample->sample, fontData);

    if ((sample->size != 0) && (sample->isRelocated != true)) {
        sample->loop = RELOC_TO_RAM(sample->loop, fontData);
        sample->book = RELOC_TO_RAM(sample->book, fontData);

        if (sample->medium == 0) {
            sample->sampleAddr = RELOC_TO_RAM(sample->sampleAddr, sampleBankReloc->baseAddr1);
            sample->medium = sampleBankReloc->medium1;
        } else if (sample->medium == 1) {
            sample->sampleAddr = RELOC_TO_RAM(sample->sampleAddr, sampleBankReloc->baseAddr2);
            sample->medium = sampleBankReloc->medium2;
        }

        sample->isRelocated = true;
    }
}

// ======== MEMORY FUNCTIONS ========

bool AudioApi_GrowSoundFontTable() {
    u16 oldCapacity = soundFontTableCapacity;
    u16 newCapacity = soundFontTableCapacity << 1;
    size_t oldSize, newSize;
    AudioTable* newSoundFontTable = NULL;

    // TODO: gAudioCtx.fontLoadStatus and gAudioCtx.soundFontList

    // Grow gAudioCtx.soundFontTable
    oldSize = sizeof(AudioTableHeader) + oldCapacity * sizeof(AudioTableEntry);
    newSize = sizeof(AudioTableHeader) + newCapacity * sizeof(AudioTableEntry);
    newSoundFontTable = recomp_alloc(newSize);
    if (!newSoundFontTable) {
        goto cleanup;
    }
    Lib_MemSet(newSoundFontTable, 0, newSize);
    Lib_MemCpy(newSoundFontTable, gAudioCtx.soundFontTable, oldSize);

    // Store new table
    recomp_printf("AudioApi: Resized soundfont table to %d\n", newCapacity);
    gAudioCtx.soundFontTable = newSoundFontTable;
    soundFontTableCapacity = newCapacity;
    return true;

 cleanup:
    recomp_printf("AudioApi: Error resizing soundfont table to %d\n", newCapacity);
    if (newSoundFontTable != NULL) {
        recomp_free(newSoundFontTable);
    }
    return false;
}

bool AudioApi_GrowInstrumentList(CustomSoundFont** soundFontPtr) {
    CustomSoundFont* oldSoundFont = *soundFontPtr;
    CustomSoundFont* newSoundFont = NULL;
    u16 oldCapacity = oldSoundFont->instrumentsCapacity;
    u16 newCapacity = oldSoundFont->instrumentsCapacity << 1;
    size_t oldSize = sizeof(CustomSoundFont) + sizeof(uintptr_t) * oldCapacity;
    size_t newSize = sizeof(CustomSoundFont) + sizeof(uintptr_t) * newCapacity;

    newSoundFont = recomp_alloc(newSize);
    if (!newSoundFont) {
        return false;
    }
    Lib_MemSet(newSoundFont, 0, newSize);
    Lib_MemCpy(newSoundFont, oldSoundFont, oldSize);

    if (IS_RECOMP_ALLOC(oldSoundFont)) {
        recomp_free(oldSoundFont);
    }
    newSoundFont->instrumentsCapacity = newCapacity;
    *soundFontPtr = newSoundFont;
    return true;
}

bool AudioApi_GrowDrumList(CustomSoundFont* soundFont) {
    Drum** newDrumList = NULL;
    u16 oldCapacity = soundFont->drumsCapacity;
    u16 newCapacity = soundFont->drumsCapacity << 1;
    size_t oldSize = sizeof(uintptr_t) * oldCapacity;
    size_t newSize = sizeof(uintptr_t) * newCapacity;

    newDrumList = recomp_alloc(newSize);
    if (!newDrumList) {
        return false;
    }
    Lib_MemSet(newDrumList, 0, newSize);
    Lib_MemCpy(newDrumList, soundFont->drums, oldSize);

    if (IS_RECOMP_ALLOC(soundFont->drums)) {
        recomp_free(soundFont->drums);
    }
    soundFont->drumsCapacity = newCapacity;
    soundFont->drums = newDrumList;
    return true;
}

bool AudioApi_GrowSoundEffectList(CustomSoundFont* soundFont) {
    SoundEffect* newSfxList = NULL;
    u16 oldCapacity = soundFont->sfxCapacity;
    u16 newCapacity = soundFont->sfxCapacity << 1;
    size_t oldSize = sizeof(SoundEffect) * oldCapacity;
    size_t newSize = sizeof(SoundEffect) * newCapacity;

    newSfxList = recomp_alloc(newSize);
    if (!newSfxList) {
        return false;
    }
    Lib_MemSet(newSfxList, 0, newSize);
    Lib_MemCpy(newSfxList, soundFont->soundEffects, oldSize);

    if (IS_RECOMP_ALLOC(soundFont->soundEffects)) {
        recomp_free(soundFont->soundEffects);
    }
    soundFont->sfxCapacity = newCapacity;
    soundFont->soundEffects = newSfxList;
    return true;
}

Drum* AudioApi_CopyDrum(Drum* src) {
    if (!src) return NULL;

    Drum* copy = recomp_alloc(sizeof(Drum));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Drum));
    copy->isRelocated = true;

    if (src->tunedSample.sample) {
        copy->tunedSample.sample = AudioApi_CopySample(src->tunedSample.sample);
        if (!copy->tunedSample.sample) {
            AudioApi_FreeDrum(copy);
            return NULL;
        }
        copy->isRelocated &= copy->tunedSample.sample->isRelocated;
    }

    if (src->envelope) {
        size_t envCount = 0;
        while (src->envelope[envCount].delay != ADSR_HANG) {
            envCount++;
        }
        envCount++;

        copy->envelope = recomp_alloc(sizeof(EnvelopePoint) * envCount);
        if (!copy->envelope) {
            AudioApi_FreeDrum(copy);
            return NULL;
        }
        Lib_MemCpy(copy->envelope, src->envelope, sizeof(EnvelopePoint) * envCount);
    }

    return copy;
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
    copy->isRelocated = true;

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
        copy->isRelocated &= copy->lowPitchTunedSample.sample->isRelocated;
    }

    if (src->normalPitchTunedSample.sample) {
        copy->normalPitchTunedSample.sample = AudioApi_CopySample(src->normalPitchTunedSample.sample);
        if (!copy->normalPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
        copy->isRelocated &= copy->normalPitchTunedSample.sample->isRelocated;
    }

    if (src->highPitchTunedSample.sample) {
        copy->highPitchTunedSample.sample = AudioApi_CopySample(src->highPitchTunedSample.sample);
        if (!copy->highPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
        copy->isRelocated &= copy->highPitchTunedSample.sample->isRelocated;
    }

    return copy;
}

Fnv32_t AudioApi_HashSample(Sample* sample) {
    if (!sample) return 0;

    Fnv32_t hval = FNV1_32A_INIT;
    hval = fnv_32a_buf(sample, 0x8, hval);

    if (sample->loop) {
        size_t loopSize = (sample->loop->header.count != 0) ? sizeof(AdpcmLoop) : sizeof(AdpcmLoopHeader);
        hval = fnv_32a_buf(sample->loop, loopSize, hval);
    }

    if (sample->book) {
        s32 order = sample->book->header.order;
        s32 numPredictors = sample->book->header.numPredictors;
        size_t bookSize = sizeof(AdpcmBookHeader) + sizeof(s16) * 8 * order * numPredictors;
        hval = fnv_32a_buf(sample->book, bookSize, hval);
    }

    return hval;
}


Sample* AudioApi_CopySample(Sample* src) {
    if (!src) return NULL;

    Fnv32_t hval;
    uintptr_t dupe;

    // ADPCM samples need to have their codeBook and predictorState data moved onto the audio heap.
    // To avoid having duplicated data on the heap, we will hash the sample struct and return an
    // existing instance if available.
    if (src->codec == CODEC_ADPCM || src->codec == CODEC_SMALL_ADPCM) {
        hval = AudioApi_HashSample(src);
        if (recomputil_u32_value_hashmap_get(sampleHashmap, hval, &dupe)) {
            refcounter_inc((void*)dupe);
            return (Sample*)dupe;
        }
    }

    Sample* copy = recomp_alloc(sizeof(Sample));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Sample));

    if (IS_KSEG0(src->sampleAddr)) {
        // Custom samples are always medium cart, never preloaded, and already relocated
        copy->medium = MEDIUM_CART;
        copy->unk_bit26 = false;
        copy->isRelocated = true;
    } else {
        copy->isRelocated = false;
    }

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

    if (hval) {
        refcounter_inc(copy);
        recomputil_u32_value_hashmap_insert(sampleHashmap, hval, (uintptr_t)copy);
    }

    return copy;
}

void AudioApi_FreeDrum(Drum* drum) {
    if (!drum) return;
    if (drum->tunedSample.sample) AudioApi_FreeSample(drum->tunedSample.sample);
    if (drum->envelope) recomp_free(drum->envelope);
    recomp_free(drum);
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
    if (refcounter_dec(sample) > 0) return;
    if (sample->loop) recomp_free(sample->loop);
    if (sample->book) recomp_free(sample->book);
    recomp_free(sample);
}

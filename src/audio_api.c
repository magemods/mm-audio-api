#include "global.h"
#include "modding.h"
#include "recompdata.h"
#include "recomputils.h"
#include "util.h"
#include "audio_load.h"
#include "soundfont.h"

typedef struct {
    s32 id;
    void* value;
} MapEntry;

U32MemoryHashmapHandle instrumentMap;
U32MemoryHashmapHandle sfxMap;

// API Interface

RECOMP_DECLARE_EVENT(AudioApi_Init());

RECOMP_EXPORT s16 AudioApi_AddSequence(AudioTableEntry entry) {
    return AudioApi_AddTableEntry(&gAudioCtx.sequenceTable, entry);
}

RECOMP_EXPORT void AudioApi_ReplaceSequence(s16 id, AudioTableEntry entry) {
    AudioApi_ReplaceTableEntry(gAudioCtx.sequenceTable, id, entry);
}

RECOMP_EXPORT void AudioApi_RestoreSequence(s16 id) {
    AudioApi_RestoreTableEntry(gAudioCtx.sequenceTable, id);
}

RECOMP_EXPORT int AudioApi_ReplaceInstrument(s32 id, Instrument* instrument) {
    if (!instrument) return 0;

    Instrument* copy = AudioApi_CopyInstrument(instrument);
    if (!copy) return 0;

    MapEntry entry = { id, copy };

    u32 count = recomputil_u32_memory_hashmap_size(instrumentMap);
    if (!recomputil_u32_memory_hashmap_create(instrumentMap, count)) {
        AudioApi_FreeInstrument(copy);
        return 0;
    }

    MapEntry* entryAddr = recomputil_u32_memory_hashmap_get(instrumentMap, count);
    if (!entryAddr) {
        AudioApi_FreeInstrument(copy);
        return 0;
    }

    *entryAddr = entry;
    return 1;
}

RECOMP_EXPORT int AudioApi_ReplaceSoundEffect(s32 id, SoundEffect* sfx) {
    if (!sfx) return 0;

    SoundEffect* copy = AudioApi_CopySoundEffect(sfx);
    if (!copy) return 0;

    MapEntry entry = { id, copy };

    u32 count = recomputil_u32_memory_hashmap_size(sfxMap);
    if (!recomputil_u32_memory_hashmap_create(sfxMap, count)) {
        AudioApi_FreeSoundEffect(copy);
        return 0;
    }

    MapEntry* entryAddr = recomputil_u32_memory_hashmap_get(sfxMap, count);
    if (!entryAddr) {
        AudioApi_FreeSoundEffect(copy);
        return 0;
    }

    *entryAddr = entry;
    return 1;
}


void AudioApi_PreInit() {
    // Make copies of tables so we can resize later
    gAudioCtx.sequenceTable = AudioApi_CopyTable(gAudioCtx.sequenceTable);
    gAudioCtx.soundFontTable = AudioApi_CopyTable(gAudioCtx.soundFontTable);
    gAudioCtx.sampleBankTable = AudioApi_CopyTable(gAudioCtx.sampleBankTable);

    //
    instrumentMap = recomputil_create_u32_memory_hashmap(sizeof(MapEntry));
    sfxMap = recomputil_create_u32_memory_hashmap(sizeof(MapEntry));

    // Call event for mods to register data
    AudioApi_Init();
}

/* -------------------------------------------------------------------------------- */

static uintptr_t sDevAddr = 0;
static u8* sRamAddr = NULL;

RECOMP_HOOK("AudioLoad_SyncDma") void on_AudioLoad_SyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium) {
    sDevAddr = devAddr;
    sRamAddr = ramAddr;
}

RECOMP_HOOK_RETURN("AudioLoad_SyncDma") void on_AudioLoad_SyncDma_Return() {
    // 0x20700 is Soundfont_0
    if (sDevAddr == 0x20700) {
        uintptr_t* fontData = (uintptr_t*)sRamAddr;
        u32 i;

        for (i = 0; i < recomputil_u32_memory_hashmap_size(instrumentMap); i++) {
            MapEntry* entry = recomputil_u32_memory_hashmap_get(instrumentMap, i);
            if (!entry) continue;

            Instrument* instrument = (Instrument*)(sRamAddr + fontData[2 + entry->id]);
            *instrument = *(Instrument*)entry->value;
        }

        for (i = 0; i < recomputil_u32_memory_hashmap_size(sfxMap); i++) {
            MapEntry* entry = recomputil_u32_memory_hashmap_get(sfxMap, i);
            if (!entry) continue;

            SoundEffect* sfx = (SoundEffect*)(sRamAddr + fontData[1] + entry->id);
            *sfx = *(SoundEffect*)entry->value;
        }

    }

    // 0x46af0 is Sequence_0
    recomp_printf("loading %p %p\n", sDevAddr, sRamAddr);
    if (sDevAddr == 0x46af0) {
        // Here, we can modify sequence 0 in various ways.

        u16* tableEnvironmentPtr = (u16*)(sRamAddr + 0x23E8);

        // ----------------------------------------

        // For instruments, if we update the sample, we must also update the corresponding
        // channel's NOTEDV delay (length), otherwise it will cut off early
        u8* chanCuccoPtr = (u8*)(sRamAddr + 0x2938);
        u16 delay = 864; // num_seconds * 96 ??

        // print_bytes(chanCuccoPtr, 11);
        // C1 - ASEQ_OP_CHAN_INSTR
        // 3D - instr 61
        // 88 - ASEQ_OP_CHAN_LDLAYER 0x88 + <layerNum:b3>
        // 29 - LAYER_293E
        // 3E
        // FF - ASEQ_OP_END
        // 67 - ASEQ_OP_LAYER_NOTEDV  0x40 + 39
        // 80 - <delay:var>
        // A6
        // 69 - <velocity:u8>
        // FF - ASEQ_OP_END

        // Write Long encoded numbers a la MIDI
        chanCuccoPtr[7] = 0x80 | (delay & 0x7f00) >> 8;
        chanCuccoPtr[8] = delay & 0xFF;

        // ----------------------------------------

        // Change CHAN_EV_SMALL_DOG_BARK's layer to CHAN_EV_RUPY_FALL's layer, which is 12 bytes ahead
        u8* chanDogBarkPtr = (u8*)(sRamAddr + 0x3D0D);
        chanDogBarkPtr[2] += 0xC;
    }
}

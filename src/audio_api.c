#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "audio/Rick-Roll-Sound-Effect.xxd"

extern DmaHandler sDmaHandler;

/* Define our sample */

extern AdpcmBook SF0_StepGround_BOOK;

AdpcmLoop rickroll_LOOP = { (AdpcmLoopHeader){0, sizeof(Rick_Roll_Sound_Effect_aifc), 0, 0}, {0} };
Sample rickrollSample = {
    0, CODEC_ADPCM, 3, false, false,
    sizeof(Rick_Roll_Sound_Effect_aifc), Rick_Roll_Sound_Effect_aifc,
    &rickroll_LOOP,
    &SF0_StepGround_BOOK
};

/* -------------------------------------------------------------------------------- */

RECOMP_DECLARE_EVENT(audio_api_init());

// We need more exports for replacing samples, etc

RECOMP_EXPORT void audio_api_replace_sequence(u32 id, void* modAddr, size_t size) {
    AudioTable* table = (AudioTable*)gSequenceTable;
    AudioTableEntry* entry = &table->entries[id];

    entry->romAddr = (uintptr_t)modAddr - SEGMENT_ROM_START(Audioseq);
    entry->size = size; //ALIGN16(size);
}

RECOMP_HOOK("AudioLoad_Init") void on_AudioLoad_Init() {
    audio_api_init();
}

/* -------------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------------- */

static uintptr_t sDevAddr = 0;
static u8* sRamAddr = NULL;

RECOMP_HOOK("AudioLoad_SyncDma") void on_AudioLoad_SyncDma(uintptr_t devAddr, u8* ramAddr, size_t size, s32 medium) {
    sDevAddr = devAddr;
    sRamAddr = ramAddr;
}

RECOMP_HOOK_RETURN("AudioLoad_SyncDma") void on_AudioLoad_SyncDma_Return() {
    // Here we can modify any soundfont or sequence

    // 0x20700 is Soundfont_0
    if (sDevAddr == 0x20700) {
        uintptr_t* fontData = (uintptr_t*)sRamAddr;
        SoundEffect* soundEffect;
        Instrument* inst;

        // This won't be needed once we stop stealing other sample's book
        void* reloc;
#define AUDIO_RELOC(v, base) (reloc = (void*)((uintptr_t)(v) + (uintptr_t)(base)))
        rickrollSample.book = AUDIO_RELOC(rickrollSample.book, fontData);
#undef AUDIO_RELOC

        // One of link's slashes
        soundEffect = (SoundEffect*)(sRamAddr + fontData[1] + 224);
        soundEffect->tunedSample.sample = &rickrollSample;

        // Cucco Crows
        inst = (Instrument*)(sRamAddr + fontData[61 + 2]); // cuccoo
        inst->normalPitchTunedSample.sample = &rickrollSample;
    }

    // 0x46af0 is Sequence_0
    if (sDevAddr == 0x46af0) {
        u32 dogBarkSfx = 0x3D0D;
        char* dogPtr = (char*)(sRamAddr + 0x3D0D);
        // Change channel to CHAN_EV_RUPY_FALL, which is 12 bytes ahead
        dogPtr[2] += 0xC;
    }
}

#include <global.h>
#include <recomp/modding.h>
#include <recomp/recomputils.h>

#include <audio_api/types.h>

RECOMP_IMPORT(".", bool AudioApiNative_AddRomDesc(AudioApiRomDesc* romDesc));
RECOMP_IMPORT(".", bool AudioApiNative_AddResource(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", bool AudioApiNative_AddSampleBank(AudioApiSampleBankInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", bool AudioApiNative_AddAudioFile(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", uintptr_t AudioApi_AddDmaCallback(AudioApiDmaCallback callback, u32 arg0, u32 arg1, u32 arg2));
RECOMP_IMPORT(".", s32 AudioApi_NativeDmaCallback(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1, u32 arg2));


static AudioApiRomDesc sRomDescriptions[] = {
    {"ad69c91157f6705e8ab06c79fe08aad47bb57ba7", "OOT", "NTSC 1.0 (US)", 0x7430, 0x00B89AD0, 0x00B896A0, 0x00B8A1C0, 0},
    {"d3ecb253776cd847a5aa63d859d8c89a2f37b364", "OOT", "NTSC 1.1 (US)", 0x7430, 0x00B89C90, 0x00B89860, 0x00B8A380, 0},
    {"41b3bdc48d98c48529219919015a1af22f5057c2", "OOT", "NTSC 1.2 (US)", 0x7960, 0x00B89B40, 0x00B89710, 0x00B8A230, 0},
    {"c892bbda3993e66bd0d56a10ecd30b1ee612210f", "OOT", "NTSC 1.0 (JP)", 0x7430, 0x00B89AD0, 0x00B896A0, 0x00B8A1C0, 0},
    {"dbfc81f655187dc6fefd93fa6798face770d579d", "OOT", "NTSC 1.1 (JP)", 0x7430, 0x00B89C90, 0x00B89860, 0x00B8A380, 0},
    {"fa5f5942b27480d60243c2d52c0e93e26b9e6b86", "OOT", "NTSC 1.2 (JP)", 0x7960, 0x00B89B40, 0x00B89710, 0x00B8A230, 0},
};

// TODO, should have an event
//RECOMP_HOOK("AudioLoad_Init") void onAudioLoad_Init() {
RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_ZZInit() {
    for (s32 i = 0; i < ARRAY_COUNT(sRomDescriptions); i++) {
        AudioApiNative_AddRomDesc(&sRomDescriptions[i]);
    }
}


RECOMP_EXPORT bool AudioApi_AddResourceFromFs(AudioApiResourceInfo* info, char* dir, char* filename) {
    AudioApiResourceInfo defaultInfo = {0};

    if (info == NULL) {
        info = &defaultInfo;
    }

    return AudioApiNative_AddResource(info, dir, filename);
}

RECOMP_EXPORT bool AudioApi_AddSequenceFromFs(AudioApiSequenceInfo* info, char* dir, char* filename) {
    return AudioApi_AddResourceFromFs((AudioApiResourceInfo*)info, dir, filename);
}

RECOMP_EXPORT bool AudioApi_AddSoundFontFromFs(AudioApiSoundFontInfo* info, char* dir, char* filename) {
    return AudioApi_AddResourceFromFs((AudioApiResourceInfo*)info, dir, filename);
}

RECOMP_EXPORT bool AudioApi_AddSampleBankFromFs(AudioApiSampleBankInfo* info, char* dir, char* filename) {
    AudioApiSampleBankInfo defaultInfo = {0};

    if (info == NULL) {
        info = &defaultInfo;
    }

    return AudioApiNative_AddSampleBank(info, dir, filename);
}

RECOMP_EXPORT bool AudioApi_AddAudioFileFromFs(AudioApiFileInfo* info, char* dir, char* filename) {
    AudioApiFileInfo defaultInfo = {0};

    if (info == NULL) {
        info = &defaultInfo;
    }

    return AudioApiNative_AddAudioFile(info, dir, filename);
}

RECOMP_EXPORT uintptr_t AudioApi_GetResourceDevAddr(u32 resourceId, u32 arg1, u32 arg2) {
    return AudioApi_AddDmaCallback(AudioApi_NativeDmaCallback, resourceId, arg1, arg2);
}

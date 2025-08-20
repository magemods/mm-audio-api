#include <global.h>
#include <recomp/modding.h>

#include <audio_api/types.h>

RECOMP_IMPORT(".", bool AudioApiNative_AddResource(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", bool AudioApiNative_AddSampleBank(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", bool AudioApiNative_AddAudioFile(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", uintptr_t AudioApi_AddDmaCallback(AudioApiDmaCallback callback, u32 arg0, u32 arg1, u32 arg2));
RECOMP_IMPORT(".", s32 AudioApi_NativeDmaCallback(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1, u32 arg2));

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

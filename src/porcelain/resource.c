#include <global.h>
#include <recomp/modding.h>

#include <audio_api/types.h>

RECOMP_IMPORT(".", bool AudioApiNative_AddResource(AudioApiResourceInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", uintptr_t AudioApi_AddDmaCallback(AudioApiDmaCallback callback, u32 arg0, u32 arg1, u32 arg2));
RECOMP_IMPORT(".", s32 AudioApi_NativeDmaCallback(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1, u32 arg2));

RECOMP_EXPORT bool AudioApi_AddResource(AudioApiResourceInfo* info, char* dir, char* filename) {
    AudioApiResourceInfo defaultInfo = {0};

    if (info == NULL) {
        info = &defaultInfo;
    }

    return AudioApiNative_AddResource(info, dir, filename);
}

RECOMP_EXPORT uintptr_t AudioApi_GetResourceDevAddr(u32 resourceId) {
    return AudioApi_AddDmaCallback(AudioApi_NativeDmaCallback, resourceId, 0, 0);
}

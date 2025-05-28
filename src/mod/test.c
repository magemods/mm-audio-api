#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "audioapi.h"

RECOMP_IMPORT(".", void audio_api_replace_sequence(u32 id, void* modAddr, size_t size));

RECOMP_CALLBACK(".", AudioApi_Init) void my_audio_api_init() {
    //audio_api_replace_sequence(NA_BGM_FILE_SELECT, __03_zseq, sizeof(__03_zseq));

    s32 size = AudioApiNative_GetSequenceSize();
    recomp_printf("Sequence size: %d\n", size);
}

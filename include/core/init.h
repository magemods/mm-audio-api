#ifndef __AUDIO_API_INIT__
#define __AUDIO_API_INIT__

#include <global.h>

#define FREQ_FACTOR 1.5f // how much to scale the game's output, i.e. 32kHz -> 48kHz
#define NUM_SUB_UPDATES 2 // further subdivide each update per frame to avoid DMEM issues

#undef AIBUF_LEN
#undef AIBUF_SIZE
#define AIBUF_LEN (88 * SAMPLES_PER_FRAME * 2) // number of samples
#define AIBUF_SIZE (AIBUF_LEN * SAMPLE_SIZE) // number of bytes

typedef enum {
    AUDIOAPI_INIT_NOT_READY,
    AUDIOAPI_INIT_QUEUEING,
    AUDIOAPI_INIT_QUEUED,
    AUDIOAPI_INIT_READY,
} AudioApiInitPhase;

extern AudioApiInitPhase gAudioApiInitPhase;

#endif

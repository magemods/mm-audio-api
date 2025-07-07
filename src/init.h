#ifndef __AUDIO_API_INIT__
#define __AUDIO_API_INIT__

#include "global.h"

typedef enum {
    AUDIOAPI_INIT_NOT_READY,
    AUDIOAPI_INIT_QUEUEING,
    AUDIOAPI_INIT_QUEUED,
    AUDIOAPI_INIT_READY,
} AudioApiInitPhase;

extern AudioApiInitPhase gAudioApiInitPhase;

#endif

#ifndef __AUDIO_API_EFFECTS__
#define __AUDIO_API_EFFECTS__

#include <global.h>

Acmd* AudioApi_ApplyCombFilter(Acmd* cmd, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                               s32 numSamplesPerUpdate);

#endif

#ifndef __AUDIO_API_SOUNDFONT__
#define __AUDIO_API_SOUNDFONT__

#include "global.h"

Instrument* AudioApi_CopyInstrument(Instrument* src);
SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src);
Sample* AudioApi_CopySample(Sample* src);

void AudioApi_FreeInstrument(Instrument* instrument);
void AudioApi_FreeSoundEffect(SoundEffect* sfx);
void AudioApi_FreeSample(Sample* sample);

#endif

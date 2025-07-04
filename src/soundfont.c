#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "queue.h"

AudioApiQueue* soundFontInitQueue;
AudioApiQueue* soundFontLoadQueue;

void AudioApi_SoundFontQueueDrain(AudioApiCmd* cmd);
Drum* AudioApi_CopyDrum(Drum* src);
SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src);
Instrument* AudioApi_CopyInstrument(Instrument* src);
Sample* AudioApi_CopySample(Sample* src);
void AudioApi_FreeDrum(Drum* drum);
void AudioApi_FreeSoundEffect(SoundEffect* sfx);
void AudioApi_FreeInstrument(Instrument* instrument);
void AudioApi_FreeSample(Sample* sample);

RECOMP_DECLARE_EVENT(AudioApi_SoundFontLoaded(s32 fontId, u8* ramAddr));

RECOMP_CALLBACK(".", AudioApi_InitInternal) void AudioApi_SoundFontInit() {
    // Queue for the init phase so that mods can register data in the correct order
    soundFontInitQueue = AudioApi_QueueCreate();
    // Queue for when a soundfont is actually loaded in order to apply our changes
    soundFontLoadQueue = AudioApi_QueueCreate();
}

RECOMP_CALLBACK(".", AudioApi_ReadyInternal) void AudioApi_SoundFontReady() {
    AudioApi_QueueDrain(soundFontInitQueue, AudioApi_SoundFontQueueDrain);
    AudioApi_QueueDestroy(soundFontInitQueue);
}

RECOMP_EXPORT void AudioApi_ReplaceDrum(s32 fontId, s32 drumId, Drum* drum) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    Drum* copy = AudioApi_CopyDrum(drum);
    if (!copy) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioApi_QueueCmd(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_DRUM, fontId, drumId, (void**)&copy);
    } else {
        AudioApi_QueueCmd(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_DRUM, fontId, drumId, (void**)&copy);
    }
}

RECOMP_EXPORT void AudioApi_ReplaceSoundEffect(s32 fontId, s32 sfxId, SoundEffect* sfx) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    SoundEffect* copy = AudioApi_CopySoundEffect(sfx);
    if (!copy) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioApi_QueueCmd(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT, fontId, sfxId, (void**)&copy);
    } else {
        AudioApi_QueueCmd(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT, fontId, sfxId, (void**)&copy);
    }
}

RECOMP_EXPORT void AudioApi_ReplaceInstrument(s32 fontId, s32 instId, Instrument* instrument) {
    if (gAudioApiInitPhase == AUDIOAPI_INIT_NOT_READY) {
        return;
    }
    Instrument* copy = AudioApi_CopyInstrument(instrument);
    if (!copy) {
        return;
    }
    if (gAudioApiInitPhase == AUDIOAPI_INIT_QUEUEING) {
        AudioApi_QueueCmd(soundFontInitQueue, AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT, fontId, instId, (void**)&copy);
    } else {
        AudioApi_QueueCmd(soundFontLoadQueue, AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT, fontId, instId, (void**)&copy);
    }
}

void AudioApi_SoundFontQueueDrain(AudioApiCmd* cmd) {
    switch (cmd->op) {
    case AUDIOAPI_CMD_OP_REPLACE_DRUM:
    case AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT:
    case AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT:
        // Move to load queue
        AudioApi_QueueCmd(soundFontLoadQueue, cmd->op, cmd->arg0, cmd->arg1, &cmd->data);
        break;
    default:
        break;
    }
}

void AudioApi_ApplySoundFontChanges(s32 fontId, u8* ramAddr) {
    uintptr_t* fontData = (uintptr_t*)ramAddr;
    AudioApiCmd* cmd;
    Drum** drumOffsets;
    Drum* drum;
    SoundEffect* sfx;
    Instrument* instrument;
    s32 numDrums = gAudioCtx.soundFontList[fontId].numDrums;
    s32 numInstruments = gAudioCtx.soundFontList[fontId].numInstruments;
    s32 numSfx = gAudioCtx.soundFontList[fontId].numSfx;

    // The first u32 in fontData is an offset to a list of offsets to the drums
    // The second u32 in fontData is an offset to the first sound effect entry
    // Starting from the 3rd u32 in fontData is the list of offsets to the instruments
    for (s32 i = 0; i < soundFontLoadQueue->numEntries; i++) {
        cmd = &soundFontLoadQueue->entries[i];
        if (cmd->arg0 != fontId) {
            continue;
        }
        switch (cmd->op) {
        case AUDIOAPI_CMD_OP_REPLACE_DRUM:
            if (cmd->arg1 >= numDrums) break;
            drumOffsets = (Drum**)(ramAddr + fontData[0]);
            drumOffsets[cmd->arg1] = (void*)((uintptr_t)cmd->asPtr - (uintptr_t)ramAddr);
            break;
        case AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT:
            if (cmd->arg1 >= numSfx) break;
            sfx = (SoundEffect*)(ramAddr + fontData[1]) + cmd->arg1;
            *sfx = *(SoundEffect*)cmd->asPtr;
            break;
        case AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT:
            if (cmd->arg1 >= numInstruments) break;
            instrument = (Instrument*)(ramAddr + fontData[2 + cmd->arg1]);
            *instrument = *(Instrument*)cmd->asPtr;
            break;
        default:
            break;
        }
    }
}

RECOMP_HOOK("AudioLoad_RelocateFont") void AudioApi_onRelocateFont(s32 fontId, void* ramAddr) {
    AudioApi_ApplySoundFontChanges(fontId, ramAddr);
    AudioApi_SoundFontLoaded(fontId, ramAddr);
}

Drum* AudioApi_CopyDrum(Drum* src) {
    if (!src) return NULL;

    Drum* copy = recomp_alloc(sizeof(Drum));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Drum));
    copy->isRelocated = 1;

    if (src->tunedSample.sample) {
        copy->tunedSample.sample = AudioApi_CopySample(src->tunedSample.sample);
        if (!copy->tunedSample.sample) {
            AudioApi_FreeDrum(copy);
            return NULL;
        }
    }

    if (src->envelope) {
        size_t envCount = 0;
        while (src->envelope[envCount].delay != ADSR_HANG) {
            envCount++;
        }
        envCount++;

        copy->envelope = recomp_alloc(sizeof(EnvelopePoint) * envCount);
        if (!copy->envelope) {
            AudioApi_FreeDrum(copy);
            return NULL;
        }
        Lib_MemCpy(copy->envelope, src->envelope, sizeof(EnvelopePoint) * envCount);
    }

    return copy;
}

SoundEffect* AudioApi_CopySoundEffect(SoundEffect* src) {
    if (!src) return NULL;

    SoundEffect* copy = recomp_alloc(sizeof(SoundEffect));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(SoundEffect));

    if (src->tunedSample.sample) {
        copy->tunedSample.sample = AudioApi_CopySample(src->tunedSample.sample);
        if (!copy->tunedSample.sample) {
            AudioApi_FreeSoundEffect(copy);
            return NULL;
        }
    }

    return copy;
}

Instrument* AudioApi_CopyInstrument(Instrument* src) {
    if (!src) return NULL;

    Instrument* copy = recomp_alloc(sizeof(Instrument));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Instrument));
    copy->isRelocated = 1;

    if (src->envelope) {
        size_t envCount = 0;
        while (src->envelope[envCount].delay != ADSR_HANG) {
            envCount++;
        }
        envCount++;

        copy->envelope = recomp_alloc(sizeof(EnvelopePoint) * envCount);
        if (!copy->envelope) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
        Lib_MemCpy(copy->envelope, src->envelope, sizeof(EnvelopePoint) * envCount);
    }

    if (src->lowPitchTunedSample.sample) {
        copy->lowPitchTunedSample.sample = AudioApi_CopySample(src->lowPitchTunedSample.sample);
        if (!copy->lowPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    if (src->normalPitchTunedSample.sample) {
        copy->normalPitchTunedSample.sample = AudioApi_CopySample(src->normalPitchTunedSample.sample);
        if (!copy->normalPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    if (src->highPitchTunedSample.sample) {
        copy->highPitchTunedSample.sample = AudioApi_CopySample(src->highPitchTunedSample.sample);
        if (!copy->highPitchTunedSample.sample) {
            AudioApi_FreeInstrument(copy);
            return NULL;
        }
    }

    return copy;
}

Sample* AudioApi_CopySample(Sample* src) {
    if (!src) return NULL;

    Sample* copy = recomp_alloc(sizeof(Sample));
    if (!copy) return NULL;

    Lib_MemCpy(copy, src, sizeof(Sample));
    copy->medium = MEDIUM_CART;
    copy->isRelocated = 1;

    if (src->loop) {
        // s16 predictorState[16] only exists if count != 0.
        size_t loopSize = (src->loop->header.count != 0) ? sizeof(AdpcmLoop) : sizeof(AdpcmLoopHeader);

        copy->loop = recomp_alloc(loopSize);
        if (!copy->loop) {
            AudioApi_FreeSample(copy);
            return NULL;
        }
        Lib_MemCpy(copy->loop, src->loop, loopSize);
    }

    if (src->book) {
        s32 order = src->book->header.order;
        s32 numPredictors = src->book->header.numPredictors;
        size_t bookSize = sizeof(AdpcmBookHeader) + sizeof(s16) * 8 * order * numPredictors;

        copy->book = recomp_alloc(bookSize);
        if (!copy->book) {
            AudioApi_FreeSample(copy);
            return NULL;
        }
        Lib_MemCpy(copy->book, src->book, bookSize);
    }

    return copy;
}

void AudioApi_FreeDrum(Drum* drum) {
    if (!drum) return;
    if (drum->tunedSample.sample) AudioApi_FreeSample(drum->tunedSample.sample);
    if (drum->envelope) recomp_free(drum->envelope);
    recomp_free(drum);
}

void AudioApi_FreeSoundEffect(SoundEffect* sfx) {
    if (!sfx) return;
    if (sfx->tunedSample.sample) AudioApi_FreeSample(sfx->tunedSample.sample);
    recomp_free(sfx);
}

void AudioApi_FreeInstrument(Instrument* instrument) {
    if (!instrument) return;
    if (instrument->envelope) recomp_free(instrument->envelope);
    if (instrument->lowPitchTunedSample.sample) AudioApi_FreeSample(instrument->lowPitchTunedSample.sample);
    if (instrument->normalPitchTunedSample.sample) AudioApi_FreeSample(instrument->normalPitchTunedSample.sample);
    if (instrument->highPitchTunedSample.sample) AudioApi_FreeSample(instrument->highPitchTunedSample.sample);
    recomp_free(instrument);
}

void AudioApi_FreeSample(Sample* sample) {
    if (!sample) return;
    if (sample->loop) recomp_free(sample->loop);
    if (sample->book) recomp_free(sample->book);
    recomp_free(sample);
}

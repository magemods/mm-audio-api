#ifndef __AUDIO_API_QUEUE__
#define __AUDIO_API_QUEUE__

#include "global.h"

typedef struct {
    u32 op;
    u32 arg0;
    u32 arg1;
    union {
        void* data;
        f32 asFloat;
        s32 asInt;
        u16 asUShort;
        s8 asSbyte;
        u8 asUbyte;
        u32 asUInt;
        void* asPtr;
    };
} AudioApiCmd;

typedef struct {
    AudioApiCmd* entries;
    u16 numEntries;
    u16 capacity;
} AudioApiQueue;

AudioApiQueue* AudioApi_QueueCreate();
void AudioApi_QueueDestroy(AudioApiQueue* queue);
bool AudioApi_QueueCmd(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1, void** data);
bool AudioApi_QueueCmdIfNotQueued(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1, void** data);
bool AudioApi_QueueIsCmdNotQueued(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1);
void AudioApi_QueueDrain(AudioApiQueue* queue, void (*drainFunc)(AudioApiCmd* cmd));
void AudioApi_QueueEmpty(AudioApiQueue* queue);

#endif

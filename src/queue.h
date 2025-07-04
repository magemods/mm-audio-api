#ifndef __AUDIO_API_QUEUE__
#define __AUDIO_API_QUEUE__

#include "global.h"

typedef enum {
    // Global Commands
    AUDIOAPI_CMD_OP_NOOP,

    // Sequence Commands
    AUDIOAPI_CMD_OP_REPLACE_SEQUENCE,
    AUDIOAPI_CMD_OP_REPLACE_SEQUENCE_FONT,
    AUDIOAPI_CMD_OP_SET_SEQUENCE_FLAGS,

    // Soundfont Commands
    AUDIOAPI_CMD_OP_ADD_DRUM,
    AUDIOAPI_CMD_OP_REPLACE_DRUM,
    AUDIOAPI_CMD_OP_ADD_SOUNDEFFECT,
    AUDIOAPI_CMD_OP_REPLACE_SOUNDEFFECT,
    AUDIOAPI_CMD_OP_ADD_INSTRUMENT,
    AUDIOAPI_CMD_OP_REPLACE_INSTRUMENT,
} AudioApiCmdOp;

typedef struct {
    AudioApiCmdOp op;
    s32 arg0;
    s32 arg1;
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

typedef enum {
    AUDIOAPI_INIT_NOT_READY,
    AUDIOAPI_INIT_QUEUEING,
    AUDIOAPI_INIT_QUEUED,
    AUDIOAPI_INIT_READY,
} AudioApiInitPhase;

extern AudioApiInitPhase gAudioApiInitPhase;

AudioApiQueue* AudioApi_QueueCreate();
void AudioApi_QueueDestroy(AudioApiQueue* queue);
bool AudioApi_QueueCmd(AudioApiQueue* queue, u8 op, s32 arg0, s32 arg1, void** data);
void AudioApi_QueueDrain(AudioApiQueue* queue, void (*drainFunc)(AudioApiCmd* cmd));

#endif

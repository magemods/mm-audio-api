#ifndef __RECOMP_QUEUE__
#define __RECOMP_QUEUE__

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
} RecompQueueCmd;

typedef struct {
    RecompQueueCmd* entries;
    u16 numEntries;
    u16 capacity;
} RecompQueue;

RecompQueue* RecompQueue_Create();
void RecompQueue_Destroy(RecompQueue* queue);
bool RecompQueue_Push(RecompQueue* queue, u32 op, u32 arg0, u32 arg1, void** data);
bool RecompQueue_PushIfNotQueued(RecompQueue* queue, u32 op, u32 arg0, u32 arg1, void** data);
bool RecompQueue_IsCmdNotQueued(RecompQueue* queue, u32 op, u32 arg0, u32 arg1);
void RecompQueue_Drain(RecompQueue* queue, void (*drainFunc)(RecompQueueCmd* cmd));
void RecompQueue_Empty(RecompQueue* queue);

#endif

#include "queue.h"
#include "modding.h"
#include "recomputils.h"

#define QUEUE_INITIAL_CAPACITY 16

AudioApiInitPhase gAudioApiInitPhase = AUDIOAPI_INIT_NOT_READY;

AudioApiQueue* AudioApi_QueueCreate() {
    AudioApiQueue* queue = recomp_alloc(sizeof(AudioApiQueue));
    if (!queue) return NULL;

    queue->entries = recomp_alloc(sizeof(AudioApiCmd) * QUEUE_INITIAL_CAPACITY);
    if (!queue->entries) {
        recomp_free(queue);
        return NULL;
    }

    queue->numEntries = 0;
    queue->capacity = QUEUE_INITIAL_CAPACITY;
    return queue;
}

bool AudioApi_QueueGrow(AudioApiQueue* queue) {
    u16 oldCapacity = queue->capacity;
    u16 newCapacity = queue->capacity << 1;
    size_t oldSize = sizeof(AudioApiCmd) * oldCapacity;
    size_t newSize = sizeof(AudioApiCmd) * newCapacity;

    AudioApiCmd* newEntries = recomp_alloc(newSize);
    if (!newEntries) {
        return false;
    }
    Lib_MemSet(newEntries, 0, newSize);
    Lib_MemCpy(newEntries, queue->entries, oldSize);
    recomp_free(queue->entries);

    queue->entries = newEntries;
    queue->capacity = newCapacity;
    return true;
}


void AudioApi_QueueDestroy(AudioApiQueue* queue) {
    if (!queue) return;
    recomp_free(queue->entries);
    recomp_free(queue);
}

bool AudioApi_QueueCmd(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1, void** data) {
    if (queue->numEntries >= queue->capacity) {
        if (!AudioApi_QueueGrow(queue)) {
            return false;
        }
    }
    queue->entries[queue->numEntries++] = (AudioApiCmd){ op, arg0, arg1, (data ? *data : NULL) };
    return true;
}

bool AudioApi_QueueCmdIfNotQueued(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1, void** data) {
    if (!AudioApi_QueueIsCmdNotQueued(queue, op, arg0, arg1)) {
        return false;
    }
    return AudioApi_QueueCmd(queue, op, arg0, arg1, data);
}

bool AudioApi_QueueIsCmdNotQueued(AudioApiQueue* queue, u32 op, u32 arg0, u32 arg1) {
    for (s32 i = 0; i < queue->numEntries; i++) {
        AudioApiCmd* cmd = &queue->entries[i];
        if (cmd->op == op && cmd->arg0 == arg0 && cmd->arg1 == arg1) {
            return false;
        }
    }
    return true;
}

void AudioApi_QueueDrain(AudioApiQueue* queue, void (*drainFunc)(AudioApiCmd* cmd)) {
    for (s32 i = 0; i < queue->numEntries; i++) {
        drainFunc(&queue->entries[i]);
    }
    queue->numEntries = 0;
}

void AudioApi_QueueEmpty(AudioApiQueue* queue) {
    queue->numEntries = 0;
}

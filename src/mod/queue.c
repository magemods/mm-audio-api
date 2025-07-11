#include "queue.h"
#include "modding.h"
#include "recomputils.h"

/**
 * This file provides a generic queue implementation for recomp inspired by the global audio command
 * queue from Majora's Mask. It is especially useful for API mods that need to provide client mods
 * a way to register data, ensuring that the correct loading order is respected.
 *
 * ```c
 * #include "modding.h"
 * #include "queue.h"
 *
 * typedef enum {
 *     MYMOD_DO_ACTION_A,
 *     MYMOD_DO_ACTION_B,
 *     MYMOD_DO_ACTION_C,
 * } MyModQueueOp;
 *
 * RecompQueue* myQueue;
 *
 * void MyMod_DrainQueue(RecompQueueCmd* cmd);
 *
 * RECOMP_CALLBACK("*", recomp_on_init) void my_mod_init() {
 *     myQueue = RecompQueue_Create();
 *
 *     // These can hold any data you like
 *     u32 arg0, arg1;
 *
 *     s32 myInt = 42;
 *     RecompQueue_Push(myQueue, MYMOD_DO_ACTION_A, arg0, arg1, (void**)&myInt);
 *
 *     f32 myFloat = 42.0f;
 *     RecompQueue_Push(myQueue, MYMOD_DO_ACTION_B, arg0, arg1, (void**)&myFloat);
 *
 *     Actor* myPtr = someObj->actor;
 *     RecompQueue_Push(myQueue, MYMOD_DO_ACTION_C, arg0, arg1, (void**)&myPtr);
 *
 *     // Call MyMod_QueueDrain() once for each entry in the queue
 *     RecompQueue_Drain(myQueue, MyMod_QueueDrain);
 *
 *     // If you're done destroy it, or leave open
 *     RecompQueue_Destroy(myQueue);
 * }
 *
 * void MyMod_DrainQueue(RecompQueueCmd* cmd) {
 *     switch (cmd->op) {
 *     case MYMOD_DO_ACTION_A:
 *         recomp_printf("A: arg0: %d, arg1: %d, data: %d\n", cmd->arg0, cmd->arg1, cmd->asInt);
 *         break;
 *     case MYMOD_DO_ACTION_B:
 *         recomp_printf("B: arg0: %d, arg1: %d, data: %.2f\n", cmd->arg0, cmd->arg1, cmd->asFloat);
 *         break;
 *     case MYMOD_DO_ACTION_C:
 *         recomp_printf("C: arg0: %d, arg1: %d, data: %p\n", cmd->arg0, cmd->arg1, cmd->asPtr);
 *         recomp_free(cmd->asPtr);
 *         break;
 *     default:
 *         break;
 *     }
 * }
 * ```
 */

#define QUEUE_INITIAL_CAPACITY 16

RecompQueue* RecompQueue_Create() {
    RecompQueue* queue = recomp_alloc(sizeof(RecompQueue));
    if (!queue) return NULL;

    queue->entries = recomp_alloc(sizeof(RecompQueueCmd) * QUEUE_INITIAL_CAPACITY);
    if (!queue->entries) {
        recomp_free(queue);
        return NULL;
    }

    queue->numEntries = 0;
    queue->capacity = QUEUE_INITIAL_CAPACITY;
    return queue;
}

bool RecompQueue_Grow(RecompQueue* queue) {
    u16 oldCapacity = queue->capacity;
    u16 newCapacity = queue->capacity << 1;
    size_t oldSize = sizeof(RecompQueueCmd) * oldCapacity;
    size_t newSize = sizeof(RecompQueueCmd) * newCapacity;

    RecompQueueCmd* newEntries = recomp_alloc(newSize);
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

void RecompQueue_Destroy(RecompQueue* queue) {
    if (!queue) return;
    recomp_free(queue->entries);
    recomp_free(queue);
}

bool RecompQueue_Push(RecompQueue* queue, u32 op, u32 arg0, u32 arg1, void** data) {
    if (queue->numEntries >= queue->capacity) {
        if (!RecompQueue_Grow(queue)) {
            return false;
        }
    }
    queue->entries[queue->numEntries++] = (RecompQueueCmd){ op, arg0, arg1, (data ? *data : NULL) };
    return true;
}

bool RecompQueue_PushIfNotQueued(RecompQueue* queue, u32 op, u32 arg0, u32 arg1, void** data) {
    if (!RecompQueue_IsCmdNotQueued(queue, op, arg0, arg1)) {
        return false;
    }
    return RecompQueue_Push(queue, op, arg0, arg1, data);
}

bool RecompQueue_IsCmdNotQueued(RecompQueue* queue, u32 op, u32 arg0, u32 arg1) {
    for (s32 i = 0; i < queue->numEntries; i++) {
        RecompQueueCmd* cmd = &queue->entries[i];
        if (cmd->op == op && cmd->arg0 == arg0 && cmd->arg1 == arg1) {
            return false;
        }
    }
    return true;
}

void RecompQueue_Drain(RecompQueue* queue, void (*drainFunc)(RecompQueueCmd* cmd)) {
    for (s32 i = 0; i < queue->numEntries; i++) {
        drainFunc(&queue->entries[i]);
    }
    queue->numEntries = 0;
}

void RecompQueue_Empty(RecompQueue* queue) {
    queue->numEntries = 0;
}

#ifndef __MOD_UTILS__
#define __MOD_UTILS__

#include "global.h"

#define SIZE_8MB 0x800000
#define IS_MOD_MEMORY(x) (U32(x) >= K0BASE + SIZE_8MB)

#define FNV1_32_INIT ((Fnv32_t)0x811c9dc5)
#define FNV1_32A_INIT FNV1_32_INIT

typedef u32 Fnv32_t;

u32 refcounter_inc(void* ptr);
u32 refcounter_dec(void* ptr);
u32 refcounter_get(void* ptr);

void print_bytes(void *ptr, int size);

Fnv32_t fnv_32a_buf(void *buf, size_t len, Fnv32_t hashval);

#endif

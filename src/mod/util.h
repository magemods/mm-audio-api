#ifndef __MOD_UTILS__
#define __MOD_UTILS__

#define SIZE_8MB 0x800000
#define IS_MOD_MEMORY(x) (U32(x) >= K0BASE + SIZE_8MB)

void print_bytes(void *ptr, int size);

#endif

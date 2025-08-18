#include <utils/misc.h>
#include <libc/string.h>
#include <recomp/recompdata.h>
#include <recomp/recomputils.h>

static U32MemoryHashmapHandle refcounter;

RECOMP_CALLBACK("*", recomp_on_init) void init_refcounter() {
    refcounter = recomputil_create_u32_memory_hashmap(sizeof(u16));
}

u32 refcounter_inc(void* ptr) {
    collection_key_t key = (uintptr_t)ptr;
    if (!recomputil_u32_memory_hashmap_contains(refcounter, key)) {
        recomputil_u32_memory_hashmap_create(refcounter, key);
    }
    u16* count = (u16*)recomputil_u32_memory_hashmap_get(refcounter, key);
    if (count == NULL) {
        return 0;
    }
    return ++(*count);
}

u32 refcounter_dec(void* ptr) {
    collection_key_t key = (uintptr_t)ptr;
    u16* count = (u16*)recomputil_u32_memory_hashmap_get(refcounter, key);
    if (count == NULL) {
        return 0;
    }
    if (--(*count) == 0) {
        recomputil_u32_memory_hashmap_erase(refcounter, key);
        return 0;
    }
    return *count;
}

u32 refcounter_get(void* ptr) {
    collection_key_t key = (uintptr_t)ptr;
    u16* count = (u16*)recomputil_u32_memory_hashmap_get(refcounter, key);
    return count ? *count : 0;
}

int Utils_MemCmp(const void *a, const void *b, size_t size) {
    const char *c = a;
    const char *d = b;

    for (size_t i = 0; i < size; ++i) {
        if (*c != *d) {
            return *c - *d;
        }

        c++;
        d++;
    }

    return 0;
}

char *Utils_StrDup(const char *s) {
    char *newStr = recomp_alloc(strlen(s) + 1);

    char *c = newStr;

    while (*s != '\0') {
        *c = *s;
        s++;
        c++;
    }

    *c = '\0';

    return newStr;
}

void print_bytes(void* ptr, int size) {
    unsigned char *p = ptr;
    int i;
    for (i = 0; i < size; i++) {
        if (i % 16 == 0) {
            recomp_printf("%08x: ", &ptr[i]);
        }
        recomp_printf("%02X", p[i]);
        if (i % 16 == 15) {
            recomp_printf("\n");
        } else if (i % 2 == 1) {
            recomp_printf(" ");
        }
    }
    recomp_printf("\n");
}

/*
 * fnv_32a_buf - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a buffer
 *
 * input:
 *    buf  - start of buffer to hash
 *    len  - length of buffer in octets
 *    hval - previous hash value or 0 if first call
 *
 * returns:
 *    32 bit hash as a static hash type
 *
 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
 *      hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
 */
Fnv32_t fnv_32a_buf(void *buf, size_t len, Fnv32_t hval) {
    unsigned char *bp = (unsigned char *)buf; // start of buffer
    unsigned char *be = bp + len;             // beyond end of buffer

    // FNV-1a hash each octet in the buffer
    while (bp < be) {
        // xor the bottom with the current octet
        hval ^= (Fnv32_t)*bp++;

        // multiply by the 32 bit FNV magic prime mod 2^32
        // hval *= FNV_32_PRIME;
        hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
    }

    // return our new hash value
    return hval;
}

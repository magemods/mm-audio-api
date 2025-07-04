#include "recomputils.h"
#include "util.h"

void print_bytes(void *ptr, int size)
{
    unsigned char *p = ptr;
    int i;
    for (i = 0; i < size; i++) {
        if (i % 16 == 0) {
            recomp_printf("%08x  ", &ptr[i]);
        }
        recomp_printf("%02X ", p[i]);
        if (i % 16 == 15) {
            recomp_printf("\n");
        }
    }
    recomp_printf("\n");
}

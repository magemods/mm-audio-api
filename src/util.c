#include "recomputils.h"
#include "util.h"

void print_bytes(void *ptr, int size)
{
    unsigned char *p = ptr;
    int i;
    for (i = 0; i < size; i++) {
        recomp_printf("%02X ", p[i]);
    }
    recomp_printf("\n");
}

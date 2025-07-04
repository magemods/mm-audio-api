#ifndef __MOD_UTILS__
#define __MOD_UTILS__

#define SIZE_8MB 0x800000
#define IS_MOD_MEMORY(x) (U32(x) >= K0BASE + SIZE_8MB)

#define INCBIN(identifier, filename)                \
    asm(".pushsection .rodata\n"                    \
        "\t.local " #identifier "\n"                \
        "\t.type " #identifier ", @object\n"        \
        "\t.balign 8\n"                             \
        #identifier ":\n"                           \
        "\t.incbin \"" filename "\"\n\n"            \
                                                    \
        "\t.local " #identifier "_end\n"            \
        "\t.type " #identifier "_end, @object\n"    \
        #identifier "_end:\n"                       \
                                                    \
        "\t.balign 8\n"                             \
        "\t.popsection\n");                         \
    extern u8 identifier[];                         \
    extern u8 identifier##_end[];

void print_bytes(void *ptr, int size);

#endif

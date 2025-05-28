#ifndef __RECOMP_INCBIN__
#define __RECOMP_INCBIN__

#define INCBIN_WITH_SIZE(identifier, filename)        \
    asm(".pushsection .rodata\n"                      \
        "\t.globl " #identifier "\n"                  \
        "\t.type " #identifier ", @object\n"          \
        "\t.balign 8\n"                               \
        #identifier ":\n"                             \
        "\t.incbin "" filename ""\n"                  \
        "\t.globl " #identifier "_end\n"              \
        #identifier "_end:\n"                         \
        "\t.popsection\n");                           \
    extern u8 identifier[];                           \
    extern u8 identifier##_end[]

#endif
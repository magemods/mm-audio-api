
#ifndef __AUDIOAPI__
#define __AUDIOAPI__

#ifdef __cplusplus
extern "C" {
#endif

// The definitions in this file have to me constructed carefully.
// All numerical types should have a size of 32-bit.
// We also need to make sure that the native side treats pointers
// and unsigned, 32-bit integers
#ifdef COMBINED_INTELLISENSE
#define PACKED_STRUCT struct
#else
#define PACKED_STRUCT struct __attribute__((packed))
#endif

#define ACH_BYTE unsigned char
#define ACH_U32 unsigned int

#ifdef MIPS
#define STRUCT_PTR(type) type*
#else
#define STRUCT_PTR(type) ACH_U32
#endif

    /*
typedef PACKED_STRUCT {
    STRUCT_PTR(const char) id;
    STRUCT_PTR(const char) display_name;
    STRUCT_PTR(const char) description;
    STRUCT_PTR(const ACH_BYTE) icon;
    STRUCT_PTR(const ACH_BYTE) icon_end;
    STRUCT_PTR(const char) additional_flags; // Optional. Can be NULL. Comma-seperated list.
    const AchievementRAInfo ra_info; // Optional. Can be NULL.
    STRUCT_PTR(const char) script; // Optional. Can be NULL.
} Achievement;
    */

// This is only here so VSCode will stop whining on the 'Combined' Intellisense Mode.
#ifdef COMBINED_INTELLISENSE
#undef MIPS
#else
#endif

#ifdef MIPS
#include "modding.h"
RECOMP_IMPORT(".", void AudioApiNative_Init(unsigned int log_level, unsigned const char* savepath));
//RECOMP_IMPORT(".", char* AudioApi_GetSequence());
RECOMP_IMPORT(".",  s32 AudioApiNative_GetSequenceSize());
#endif

#ifdef __cplusplus
}
#endif

#endif

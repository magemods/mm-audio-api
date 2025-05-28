
#ifndef __ACHIEVEMENTS__
#define __ACHIEVEMENTS__

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

typedef enum {
    ACHIEVEMENT_FLAG_UNSIGNED_INT,
    ACHIEVEMENT_FLAG_SIGNED_INT,
    ACHIEVEMENT_FLAG_FLOAT,
    ACHIEVEMENT_FLAG_MAX,
    ACHIEVEMENT_FLAG_SIZE = 0xFFFFFFFF
} AchievementFlagType;

#define ACHIEVEMENT_FLAG_SIZE 4

typedef PACKED_STRUCT {
    STRUCT_PTR(const char) id;
    AchievementFlagType type;
    STRUCT_PTR(const char) default_value; // Write as a string. The extlib will process this into the correct format
} AchievementFlagDefinition;

typedef PACKED_STRUCT {
    ACH_U32 ra_id;
} AchievementRAInfo;

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

// This is only here so VSCode will stop whining on the 'Combined' Intellisense Mode.
#ifdef COMBINED_INTELLISENSE
#undef MIPS
#else 
#endif

#ifdef MIPS
#include "modding.h"
RECOMP_IMPORT(".", void AchievementNative_Init(unsigned int log_level, unsigned int number_of_save_slots, unsigned const char* savepath));
RECOMP_IMPORT(".", void AchievementNative_Declare(const char* ach_set, Achievement* achievement));
RECOMP_IMPORT(".", void AchievementNative_SetU32Flag(const char* ach_set, const char* ach_id, u32 slot, u32 value));
RECOMP_IMPORT(".", Achievement* AchievementNative_GetNextAchievementUnlock());
#endif

#ifdef __cplusplus
}
#endif

#endif
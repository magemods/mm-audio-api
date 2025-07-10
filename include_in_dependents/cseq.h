#ifndef __MM_CSEQ__
#define __MM_CSEQ__

/*! \file cseq.h
    \version 0.2.0
    \brief Functions for programatically building sequence files
 */

#define MML_VERSION_OOT  0
#define MML_VERSION_MM   1
#define MML_VERSION      MML_VERSION_MM

#include "global.h"
#include "modding.h"
#include "audio/aseq.h"


typedef struct CSeqSection CSeqSection;
typedef struct CSeqOffsetPatch CSeqOffsetPatch;

typedef struct CSeqBuffer {
    u8* data;
    size_t size;
    size_t capacity;
} CSeqBuffer;

typedef struct CSeqContainer {
    CSeqBuffer* buffer;

    CSeqSection* sections;
    size_t section_count;
    size_t section_capacity;

    CSeqOffsetPatch* patches;
    size_t patch_count;
    size_t patch_capacity;
} CSeqContainer;

typedef struct CSeqOffsetPatch {
    CSeqSection* source;
    CSeqSection* target;
    size_t relative_source_offset;
} CSeqOffsetPatch;

typedef enum {
    CSEQ_SECTION_SEQUENCE,
    CSEQ_SECTION_CHANNEL,
    CSEQ_SECTION_LAYER,
    CSEQ_SECTION_TABLE,
    CSEQ_SECTION_ARRAY,
    CSEQ_SECTION_FILTER,
    CSEQ_SECTION_ENVELOPE,
    CSEQ_SECTION_BUFFER,
    CSEQ_SECTION_LABEL = 0xFF,
} CSeqSectionType;

typedef struct CSeqSection {
    CSeqContainer* root;
    CSeqSectionType type;
    union {
        CSeqBuffer* buffer;
        CSeqSection* label_target_section;
    };
    size_t offset;
    bool ended;
} CSeqSection;


RECOMP_IMPORT("magemods_audio_api", CSeqContainer* cseq_create());
RECOMP_IMPORT("magemods_audio_api", void cseq_destroy(CSeqContainer* root));
RECOMP_IMPORT("magemods_audio_api", void cseq_compile(CSeqContainer* root, size_t base_offset));

// Section functions
RECOMP_IMPORT("magemods_audio_api", CSeqSection* cseq_sequence_create(CSeqContainer* root));
RECOMP_IMPORT("magemods_audio_api", CSeqSection* cseq_channel_create(CSeqContainer* root));
RECOMP_IMPORT("magemods_audio_api", CSeqSection* cseq_layer_create(CSeqContainer* root));
RECOMP_IMPORT("magemods_audio_api", CSeqSection* cseq_label_create(CSeqSection* section));
RECOMP_IMPORT("magemods_audio_api", bool cseq_section_end(CSeqSection* section));
RECOMP_IMPORT("magemods_audio_api", void cseq_section_destroy(CSeqSection* section));

// Control flow commands
RECOMP_IMPORT("magemods_audio_api", bool cseq_loop(CSeqSection* section, u8 num));
RECOMP_IMPORT("magemods_audio_api", bool cseq_loopend(CSeqSection* section));
RECOMP_IMPORT("magemods_audio_api", bool cseq_jump(CSeqSection* section, CSeqSection* target));
RECOMP_IMPORT("magemods_audio_api", bool cseq_delay(CSeqSection* section, u16 delay));
RECOMP_IMPORT("magemods_audio_api", bool cseq_delay1(CSeqSection* section, u16 delay));

// Common commands
RECOMP_IMPORT("magemods_audio_api", bool cseq_mutebhv(CSeqSection* section, u8 flags));
RECOMP_IMPORT("magemods_audio_api", bool cseq_vol(CSeqSection* section, u8 amount));
RECOMP_IMPORT("magemods_audio_api", bool cseq_transpose(CSeqSection* section, u8 semitones));
RECOMP_IMPORT("magemods_audio_api", bool cseq_ldchan(CSeqSection* section, u8 channelNum, CSeqSection* channel));
RECOMP_IMPORT("magemods_audio_api", bool cseq_instr(CSeqSection* section, u8 instNum));

// Sequence commands
RECOMP_IMPORT("magemods_audio_api", bool cseq_mutescale(CSeqSection* sequence, u8 arg));
RECOMP_IMPORT("magemods_audio_api", bool cseq_initchan(CSeqSection* sequence, u16 bitmask));
RECOMP_IMPORT("magemods_audio_api", bool cseq_freechan(CSeqSection* sequence, u16 bitmask));
RECOMP_IMPORT("magemods_audio_api", bool cseq_tempo(CSeqSection* sequence, u8 bpm));

// Channel commands
RECOMP_IMPORT("magemods_audio_api", bool cseq_noshort(CSeqSection* section));
RECOMP_IMPORT("magemods_audio_api", bool cseq_short(CSeqSection* section));
RECOMP_IMPORT("magemods_audio_api", bool cseq_ldlayer(CSeqSection* channel, u8 layerNum, CSeqSection* layer));

// Layer commands
RECOMP_IMPORT("magemods_audio_api", bool cseq_notedvg(CSeqSection* section, u8 pitch, u16 delay, u8 velocity, u8 gateTime));
RECOMP_IMPORT("magemods_audio_api", bool cseq_notedv(CSeqSection* section, u8 pitch, u16 delay, u8 velocity));
RECOMP_IMPORT("magemods_audio_api", bool cseq_notevg(CSeqSection* section, u8 pitch, u8 velocity, u8 gateTime));

#endif

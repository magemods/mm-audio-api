#ifndef __RECOMP_CSEQ__
#define __RECOMP_CSEQ__

#define MML_VERSION_OOT  0
#define MML_VERSION_MM   1
#define MML_VERSION      MML_VERSION_MM

#include <global.h>
#include <audio/aseq.h>

#define CSEQ_DEFAULT_SEQUENCE_SECTION_CAPACITY 8
#define CSEQ_DEFAULT_SEQUENCE_PATCH_CAPACITY 8
#define CSEQ_DEFAULT_SEQUENCE_BUFFER_SIZE 1024
#define CSEQ_DEFAULT_SECTION_BUFFER_SIZE 16
#define CSEQ_BUFFER_GROW_FACTOR 1.5f

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

CSeqContainer* cseq_create();
void cseq_compile(CSeqContainer* root, size_t base_offset);
void cseq_destroy(CSeqContainer* root);

// Section functions
CSeqSection* cseq_sequence_create(CSeqContainer* seq);
CSeqSection* cseq_channel_create(CSeqContainer* seq);
CSeqSection* cseq_layer_create(CSeqContainer* seq);
CSeqSection* cseq_label_create(CSeqSection* section);
bool cseq_section_end(CSeqSection* section);
void cseq_section_destroy(CSeqSection* section);

// Control flow commands
bool cseq_loop(CSeqSection* section, u8 num);
bool cseq_loopend(CSeqSection* section);
bool cseq_jump(CSeqSection* section, CSeqSection* target);
bool cseq_delay(CSeqSection* section, u16 delay);
bool cseq_delay1(CSeqSection* section, u16 delay);

// Common commands
bool cseq_mutebhv(CSeqSection* section, u8 flags);
bool cseq_vol(CSeqSection* section, u8 amount);
bool cseq_transpose(CSeqSection* section, u8 semitones);
bool cseq_ldchan(CSeqSection* section, u8 channelNum, CSeqSection* channel);
bool cseq_instr(CSeqSection* section, u8 instNum);

// Sequence commands
bool cseq_volscale(CSeqSection* sequence, u8 arg);
bool cseq_mutescale(CSeqSection* sequence, u8 arg);
bool cseq_initchan(CSeqSection* sequence, u16 bitmask);
bool cseq_freechan(CSeqSection* sequence, u16 bitmask);
bool cseq_tempo(CSeqSection* sequence, u8 bpm);

// Channel commands
bool cseq_notepri(CSeqSection* section, u8 priority);
bool cseq_font(CSeqSection* section, u8 fontId);
bool cseq_fontinstr(CSeqSection* section, u8 fontId, u8 instId);
bool cseq_noshort(CSeqSection* section);
bool cseq_short(CSeqSection* section);
bool cseq_ldlayer(CSeqSection* channel, u8 layerNum, CSeqSection* layer);
bool cseq_pan(CSeqSection* section, u8 pan);
bool cseq_panweight(CSeqSection* section, u8 weight);

// Layer commands
bool cseq_ldelay(CSeqSection* section, u16 delay);
bool cseq_notedvg(CSeqSection* section, u8 pitch, u16 delay, u8 velocity, u8 gateTime);
bool cseq_notedv(CSeqSection* section, u8 pitch, u16 delay, u8 velocity);
bool cseq_notevg(CSeqSection* section, u8 pitch, u8 velocity, u8 gateTime);
bool cseq_notepan(CSeqSection* section, u8 pan);

#endif

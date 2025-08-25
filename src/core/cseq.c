#include <core/cseq.h>
#include <recomp/modding.h>
#include <recomp/recomputils.h>

// ======== BUFFER FUNCTIONS ========

CSeqBuffer* cseq_buffer_create(size_t capacity) {
    CSeqBuffer* buf = recomp_alloc(sizeof(CSeqBuffer));
    if (!buf) return NULL;
    buf->data = recomp_alloc(capacity);
    if (!buf->data) {
        recomp_free(buf);
        return NULL;
    }
    buf->size = 0;
    buf->capacity = capacity;
    return buf;
}

bool cseq_buffer_grow(CSeqBuffer* buf, size_t new_capacity) {
    if (new_capacity <= buf->capacity) return true;

    u8* new_data = recomp_alloc(new_capacity);
    if (!new_data) {
        return false;
    }
    Lib_MemSet(new_data, 0, new_capacity);
    Lib_MemCpy(new_data, buf->data, buf->size);

    recomp_free(buf->data);
    buf->data = new_data;
    buf->capacity = new_capacity;
    return true;
}

bool cseq_buffer_append(CSeqBuffer* buf, CSeqBuffer* source) {
    size_t new_capacity = buf->capacity;
    while ((buf->size + source->size) > new_capacity) {
        new_capacity *= CSEQ_BUFFER_GROW_FACTOR;
    }
    if (!cseq_buffer_grow(buf, new_capacity)) {
        return false;
    }
    Lib_MemCpy(buf->data + buf->size, source->data, source->size);
    buf->size += source->size;
    return true;
}

bool cseq_buffer_write_u8(CSeqBuffer* buf, u8 val) {
    if (buf->size >= buf->capacity) {
        if (!cseq_buffer_grow(buf, buf->capacity * CSEQ_BUFFER_GROW_FACTOR)) {
            return false;
        }
    }
    buf->data[buf->size++] = val;
    return true;
}

bool cseq_buffer_write_u16(CSeqBuffer* buf, u16 val) {
    return cseq_buffer_write_u8(buf, (val >> 8) & 0xFF)
        && cseq_buffer_write_u8(buf, val & 0xFF);
}

bool cseq_buffer_write_var(CSeqBuffer* buf, u16 val) {
    if (val < 0x80) {
        return cseq_buffer_write_u8(buf, val);
    }
    return cseq_buffer_write_u8(buf, 0x80 | (val & 0x7f00) >> 8)
        && cseq_buffer_write_u8(buf, val & 0xFF);
}

void cseq_buffer_destroy(CSeqBuffer* buf) {
    if (!buf) return;
    if (buf->data) recomp_free(buf->data);
    recomp_free(buf);
}

// ======== CONTAINER FUNCTIONS ========

RECOMP_EXPORT CSeqContainer* cseq_create() {
    CSeqContainer* root = recomp_alloc(sizeof(CSeqContainer));
    if (!root) return NULL;

    root->buffer = cseq_buffer_create(CSEQ_DEFAULT_SEQUENCE_BUFFER_SIZE);
    if (!root->buffer) {
        goto cleanup;
    }

    root->section_count = 0;
    root->section_capacity = CSEQ_DEFAULT_SEQUENCE_SECTION_CAPACITY;
    root->sections = recomp_alloc(root->section_capacity * sizeof(CSeqSection));
    if (!root->sections) {
        goto cleanup;
    }

    root->patch_count = 0;
    root->patch_capacity = CSEQ_DEFAULT_SEQUENCE_PATCH_CAPACITY;
    root->patches = recomp_alloc(root->patch_capacity * sizeof(CSeqOffsetPatch));
    if (!root->patches) {
        goto cleanup;
    }

    return root;

 cleanup:
    cseq_destroy(root);
    return NULL;
}

RECOMP_EXPORT void cseq_destroy(CSeqContainer* root) {
    if (!root) return;
    for (size_t i = 0; i < root->section_count; i++) {
        cseq_section_destroy(&root->sections[i]);
    }
    if (root->buffer) cseq_buffer_destroy(root->buffer);
    if (root->sections) recomp_free(root->sections);
    if (root->patches) recomp_free(root->patches);
    recomp_free(root);
}

bool cseq_add_offset_patch(CSeqContainer* root, CSeqSection* source, CSeqSection* target,
                                    size_t relative_source_offset) {
    if (root->patch_count >= root->patch_capacity) {
        size_t old_capacity = root->patch_capacity;
        size_t new_capacity = root->patch_capacity << 1;
        size_t old_size = old_capacity * sizeof(CSeqOffsetPatch);
        size_t new_size = new_capacity * sizeof(CSeqOffsetPatch);

        CSeqOffsetPatch* newPatches = recomp_alloc(new_size);
        if (!newPatches) {
            return false;
        }
        Lib_MemSet(newPatches, 0, new_size);
        Lib_MemCpy(newPatches, root->patches, old_size);

        recomp_free(root->patches);
        root->patches = newPatches;
        root->patch_capacity = new_capacity;
    }
    root->patches[root->patch_count++] = (CSeqOffsetPatch){ source, target, relative_source_offset };
    return true;
}

RECOMP_EXPORT void cseq_compile(CSeqContainer* root, size_t base_offset) {
    size_t current_offset = base_offset;
    size_t i;

    // Loop through each section, write to final buffer, and calculate offsets
    for (i = 0; i < root->section_count; i++) {
        CSeqSection* section = &root->sections[i];
        if (section->type == CSEQ_SECTION_LABEL) continue;
        if (section->type == CSEQ_SECTION_SEQUENCE && !section->ended) cseq_section_end(section);
        section->offset = current_offset;
        cseq_buffer_append(root->buffer, section->buffer);
        current_offset += section->buffer->size;
    }

    // Apply patches to final buffer in order to update offset references
    for (i = 0; i < root->patch_count; i++) {
        CSeqOffsetPatch* patch = &root->patches[i];
        size_t patch_offset = patch->source->offset + patch->relative_source_offset;
        size_t target_offset = patch->target->offset;
        if (patch->target->type == CSEQ_SECTION_LABEL) {
            target_offset += patch->target->label_target_section->offset;
        }
        root->buffer->data[patch_offset + 0] = (target_offset >> 8) & 0xFF;
        root->buffer->data[patch_offset + 1] = target_offset & 0xFF;
    }
}

// ======== SECTION FUNCTIONS ========

CSeqSection* cseq_section_create(CSeqContainer* root, CSeqSectionType type) {
    if (root->section_count >= root->section_capacity) {
        size_t old_capacity = root->section_capacity;
        size_t new_capacity = root->section_capacity << 1;
        size_t old_size = old_capacity * sizeof(CSeqSection);
        size_t new_size = new_capacity * sizeof(CSeqSection);

        CSeqSection* new_sections = recomp_alloc(new_size);
        if (!new_sections) {
            return NULL;
        }
        Lib_MemSet(new_sections, 0, new_size);
        Lib_MemCpy(new_sections, root->sections, old_size);

        recomp_free(root->sections);
        root->sections = new_sections;
        root->section_capacity = new_capacity;
    }

    CSeqSection* section = &root->sections[root->section_count];
    section->root = root;
    section->type = type;
    if (type != CSEQ_SECTION_LABEL) {
        section->buffer = cseq_buffer_create(CSEQ_DEFAULT_SECTION_BUFFER_SIZE);
        if (!section->buffer) {
            return NULL;
        }
    }
    section->offset = 0;
    section->ended = false;
    root->section_count++;
    return section;
}

RECOMP_EXPORT CSeqSection* cseq_sequence_create(CSeqContainer* root) {
    return cseq_section_create(root, CSEQ_SECTION_SEQUENCE);
}

RECOMP_EXPORT CSeqSection* cseq_channel_create(CSeqContainer* root) {
    return cseq_section_create(root, CSEQ_SECTION_CHANNEL);
}

RECOMP_EXPORT CSeqSection* cseq_layer_create(CSeqContainer* root) {
    return cseq_section_create(root, CSEQ_SECTION_LAYER);
}

RECOMP_EXPORT CSeqSection* cseq_label_create(CSeqSection* section) {
    if (!section || section->ended) return NULL;
    CSeqSection* label = cseq_section_create(section->root, CSEQ_SECTION_LABEL);
    if (!label) return NULL;
    label->label_target_section = section;
    label->offset = section->buffer->size;
    label->ended = true;
    return label;
}

RECOMP_EXPORT bool cseq_section_end(CSeqSection* section) {
    if (!section || section->ended) return false;
    section->ended = cseq_buffer_write_u8(section->buffer, ASEQ_OP_END);
    return section->ended;
}

RECOMP_EXPORT void cseq_section_destroy(CSeqSection* section) {
    if (!section || section->type == CSEQ_SECTION_LABEL) return;
    cseq_buffer_destroy(section->buffer);
}

// ======== OPCODE FUNCTIONS ========

// Control flow commands

RECOMP_EXPORT bool cseq_loop(CSeqSection* section, u8 num) {
    if (!section || section->ended) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LOOP)
        && cseq_buffer_write_u8(section->buffer, num);
}

RECOMP_EXPORT bool cseq_loopend(CSeqSection* section) {
    if (!section || section->ended) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LOOPEND);
}

RECOMP_EXPORT bool cseq_jump(CSeqSection* section, CSeqSection* target) {
    if (!section || section->ended || !target) return false;
    cseq_add_offset_patch(section->root, section, target, section->buffer->size + 1);
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_JUMP)
        && cseq_buffer_write_u16(section->buffer, 0x0000);
}

RECOMP_EXPORT bool cseq_delay(CSeqSection* section, u16 delay) {
    if (!section || section->ended) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_DELAY)
        && cseq_buffer_write_var(section->buffer, delay);
}

RECOMP_EXPORT bool cseq_delay1(CSeqSection* section, u16 delay) {
    if (!section || section->ended) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_DELAY1);
}

// Common commands

RECOMP_EXPORT bool cseq_mutebhv(CSeqSection* section, u8 flags) {
    if (!section || section->ended) return false;
    u8 op = (section->type == CSEQ_SECTION_SEQUENCE) ? ASEQ_OP_SEQ_MUTEBHV
        : (section->type == CSEQ_SECTION_CHANNEL) ? ASEQ_OP_CHAN_MUTEBHV
        : 0xFF;
    if (op == 0xFF) return false;
    return cseq_buffer_write_u8(section->buffer, op)
        && cseq_buffer_write_u8(section->buffer, flags);
}

RECOMP_EXPORT bool cseq_vol(CSeqSection* section, u8 amount) {
    if (!section || section->ended) return false;
    u8 op = (section->type == CSEQ_SECTION_SEQUENCE) ? ASEQ_OP_SEQ_VOL
        : (section->type == CSEQ_SECTION_CHANNEL) ? ASEQ_OP_CHAN_VOL
        : 0xFF;
    if (op == 0xFF) return false;
    return cseq_buffer_write_u8(section->buffer, op)
        && cseq_buffer_write_u8(section->buffer, amount);
}

RECOMP_EXPORT bool cseq_transpose(CSeqSection* section, u8 semitones) {
    if (!section || section->ended) return false;
    u8 op = (section->type == CSEQ_SECTION_SEQUENCE) ? ASEQ_OP_SEQ_TRANSPOSE
        : (section->type == CSEQ_SECTION_CHANNEL) ? ASEQ_OP_CHAN_TRANSPOSE
        : (section->type == CSEQ_SECTION_LAYER) ? ASEQ_OP_LAYER_TRANSPOSE
        : 0xFF;
    if (op == 0xFF) return false;
    return cseq_buffer_write_u8(section->buffer, op)
        && cseq_buffer_write_u8(section->buffer, semitones);
}

RECOMP_EXPORT bool cseq_ldchan(CSeqSection* section, u8 channelNum, CSeqSection* channel) {
    if (!section || section->ended || !channel) return false;
    u8 op = (section->type == CSEQ_SECTION_SEQUENCE) ? ASEQ_OP_SEQ_LDCHAN
        : (section->type == CSEQ_SECTION_CHANNEL) ? ASEQ_OP_CHAN_LDCHAN
        : 0xFF;
    if (op == 0xFF) return false;
    cseq_add_offset_patch(section->root, section, channel, section->buffer->size + 1);
    return cseq_buffer_write_u8(section->buffer, op | (channelNum & 0x7))
        && cseq_buffer_write_u16(section->buffer, 0x0000);
}

RECOMP_EXPORT bool cseq_instr(CSeqSection* section, u8 instNum) {
    if (!section || section->ended) return false;
    u8 op = (section->type == CSEQ_SECTION_CHANNEL) ? ASEQ_OP_CHAN_INSTR
        : (section->type == CSEQ_SECTION_LAYER) ? ASEQ_OP_LAYER_INSTR
        : 0xFF;
    if (op == 0xFF) return false;
    return cseq_buffer_write_u8(section->buffer, op)
        && cseq_buffer_write_u8(section->buffer, instNum);
}

// Sequence commands

RECOMP_EXPORT bool cseq_volscale(CSeqSection* sequence, u8 arg) {
    if (!sequence || sequence->ended) return false;
    if (sequence->type != CSEQ_SECTION_SEQUENCE) return false;
    return cseq_buffer_write_u8(sequence->buffer, ASEQ_OP_SEQ_VOLSCALE)
        && cseq_buffer_write_u8(sequence->buffer, arg);
}

RECOMP_EXPORT bool cseq_mutescale(CSeqSection* sequence, u8 arg) {
    if (!sequence || sequence->ended) return false;
    if (sequence->type != CSEQ_SECTION_SEQUENCE) return false;
    return cseq_buffer_write_u8(sequence->buffer, ASEQ_OP_SEQ_MUTESCALE)
        && cseq_buffer_write_u8(sequence->buffer, arg);
}

RECOMP_EXPORT bool cseq_initchan(CSeqSection* sequence, u16 bitmask) {
    if (!sequence || sequence->ended) return false;
    if (sequence->type != CSEQ_SECTION_SEQUENCE) return false;
    return cseq_buffer_write_u8(sequence->buffer, ASEQ_OP_SEQ_INITCHAN)
        && cseq_buffer_write_u16(sequence->buffer, bitmask);
}

RECOMP_EXPORT bool cseq_freechan(CSeqSection* sequence, u16 bitmask) {
    if (!sequence || sequence->ended) return false;
    if (sequence->type != CSEQ_SECTION_SEQUENCE) return false;
    return cseq_buffer_write_u8(sequence->buffer, ASEQ_OP_SEQ_FREECHAN)
        && cseq_buffer_write_u16(sequence->buffer, bitmask);
}

RECOMP_EXPORT bool cseq_tempo(CSeqSection* sequence, u8 bpm) {
    if (!sequence || sequence->ended) return false;
    if (sequence->type != CSEQ_SECTION_SEQUENCE) return false;
    return cseq_buffer_write_u8(sequence->buffer, ASEQ_OP_SEQ_TEMPO)
        && cseq_buffer_write_u8(sequence->buffer, bpm);
}

// Channel commands

RECOMP_EXPORT bool cseq_notepri(CSeqSection* section, u8 priority) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_NOTEPRI)
        && cseq_buffer_write_u8(section->buffer, priority);
}

RECOMP_EXPORT bool cseq_font(CSeqSection* section, u8 fontId) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_FONT)
        && cseq_buffer_write_u8(section->buffer, fontId);
}

RECOMP_EXPORT bool cseq_fontinstr(CSeqSection* section, u8 fontId, u8 instId) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_FONTINSTR)
        && cseq_buffer_write_u8(section->buffer, fontId)
        && cseq_buffer_write_u8(section->buffer, instId);
}

RECOMP_EXPORT bool cseq_noshort(CSeqSection* section) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_NOSHORT);
}

RECOMP_EXPORT bool cseq_short(CSeqSection* section) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_SHORT);
}

RECOMP_EXPORT bool cseq_ldlayer(CSeqSection* channel, u8 layerNum, CSeqSection* layer) {
    if (!channel || channel->ended || !layer) return false;
    if (channel->type != CSEQ_SECTION_CHANNEL || layer->type != CSEQ_SECTION_LAYER) return false;
    cseq_add_offset_patch(channel->root, channel, layer, channel->buffer->size + 1);
    return cseq_buffer_write_u8(channel->buffer, ASEQ_OP_CHAN_LDLAYER | (layerNum & 0x7))
        && cseq_buffer_write_u16(channel->buffer, 0x0000);
}

RECOMP_EXPORT bool cseq_pan(CSeqSection* section, u8 pan) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_PAN)
        && cseq_buffer_write_u8(section->buffer, pan);
}

RECOMP_EXPORT bool cseq_panweight(CSeqSection* section, u8 weight) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_CHANNEL) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_CHAN_PANWEIGHT)
        && cseq_buffer_write_u8(section->buffer, weight);
}


// Layer commands

RECOMP_EXPORT bool cseq_ldelay(CSeqSection* section, u16 delay) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_LAYER) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LAYER_LDELAY)
        && cseq_buffer_write_var(section->buffer, delay);
}

RECOMP_EXPORT bool cseq_notedvg(CSeqSection* section, u8 pitch, u16 delay, u8 velocity, u8 gateTime) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_LAYER) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LAYER_NOTEDVG | (pitch & 0x3F))
        && cseq_buffer_write_var(section->buffer, delay)
        && cseq_buffer_write_u8(section->buffer, velocity)
        && cseq_buffer_write_u8(section->buffer, gateTime);
}

RECOMP_EXPORT bool cseq_notedv(CSeqSection* section, u8 pitch, u16 delay, u8 velocity) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_LAYER) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LAYER_NOTEDV | (pitch & 0x3F))
        && cseq_buffer_write_var(section->buffer, delay)
        && cseq_buffer_write_u8(section->buffer, velocity);
}

RECOMP_EXPORT bool cseq_notevg(CSeqSection* section, u8 pitch, u8 velocity, u8 gateTime) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_LAYER) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LAYER_NOTEVG | (pitch & 0x3F))
        && cseq_buffer_write_u8(section->buffer, velocity)
        && cseq_buffer_write_u8(section->buffer, gateTime);
}

RECOMP_EXPORT bool cseq_notepan(CSeqSection* section, u8 pan) {
    if (!section || section->ended) return false;
    if (section->type != CSEQ_SECTION_LAYER) return false;
    return cseq_buffer_write_u8(section->buffer, ASEQ_OP_LAYER_NOTEPAN)
        && cseq_buffer_write_u8(section->buffer, pan);
}

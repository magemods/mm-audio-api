#include <global.h>
#include <recomp/modding.h>
#include <recomp/recomputils.h>
#include <libc64/fixed_point.h>

#include <audio_api/types.h>
#include <audio_api/sequence.h>
#include <audio_api/soundfont.h>
#include <audio_api/cseq.h>

RECOMP_IMPORT(".", s32 AudioApi_AddAudioFileFromFs(AudioApiFileInfo* info, char* dir, char* filename));
RECOMP_IMPORT(".", uintptr_t AudioApi_GetResourceDevAddr(u32 resourceId, u32 arg1, u32 arg2));

RECOMP_EXPORT s32 AudioApi_CreateStreamedSequence(AudioApiFileInfo* info) {
    u32 channelCount, trackCount;
    u32 channelNo, trackNo;
    s32 seqId, fontId;
    u16 length;
    uintptr_t sampleAddr;
    AdpcmLoop sampleLoop;
    Sample sample;
    Instrument inst;
    size_t seqSize;
    u8* seqData;
    AudioTableEntry entry;
    CSeqContainer* root;
    CSeqSection* seq;
    CSeqSection* chan;
    CSeqSection* layer;
    CSeqSection* label;

    if (info == NULL) {
        return -1;
    }

    if (info->channelType == AUDIOAPI_CHANNEL_TYPE_MONO) {
        trackCount = MIN(info->trackCount, 16);
        channelCount = trackCount;
    } else if (info->channelType == AUDIOAPI_CHANNEL_TYPE_STEREO) {
        trackCount = MIN(info->trackCount, 32);
        channelCount = trackCount / 2;
    } else {
        return -1;
    }

    fontId = AudioApi_CreateEmptySoundFont();

    for (trackNo = 0; trackNo < trackCount; trackNo++) {

        sampleAddr = AudioApi_GetResourceDevAddr(info->resourceId, trackNo, 0);

        sampleLoop = (AdpcmLoop){
            { info->loopStart, info->loopEnd, info->loopCount, info->sampleCount }, {}
        };

        sample = (Sample){
            0, CODEC_S16, MEDIUM_CART, false, false,
            info->sampleCount * 2,
            (void*)sampleAddr,
            &sampleLoop,
            NULL
        };

        inst = (Instrument){
            false,
            INSTR_SAMPLE_LO_NONE,
            INSTR_SAMPLE_HI_NONE,
            251,
            DefaultEnvelopePoint,
            INSTR_SAMPLE_NONE,
            { &sample, info->sampleRate / 32000.0f },
            INSTR_SAMPLE_NONE,
        };

        AudioApi_AddInstrument(fontId, &inst);
    }

    if (info->loopCount == -1) {
        length = 0x7FFF;
    } else {
        length = lceilf((info->loopCount + 1) * ((f32)info->sampleCount / info->sampleRate) *
                        (TATUMS_PER_BEAT / 60.0f));
        length = CLAMP(length, 0, 0x7FFF);
    }

    length = lceilf(10.0f *  (TATUMS_PER_BEAT / 60.0f));


    root = cseq_create();
    seq = cseq_sequence_create(root);

    cseq_mutebhv(seq, 0x20);
    cseq_mutescale(seq, 0x32);
    cseq_initchan(seq, (1 << channelCount) - 1);
    //cseq_volscale(seq, 0x7F);

    label = cseq_label_create(seq);

    for (channelNo = 0; channelNo < channelCount; channelNo++) {
        chan = cseq_channel_create(root);
        cseq_ldchan(seq, channelNo, chan);
        cseq_noshort(chan);
        cseq_panweight(chan, 0);
        cseq_notepri(chan, 1);
        //cseq_vol(chan, 0x7F);

        if (info->channelType == AUDIOAPI_CHANNEL_TYPE_MONO) {
            layer = cseq_layer_create(root);
            cseq_ldlayer(chan, 0, layer);
            cseq_instr(layer, channelNo);
            cseq_notepan(layer, 0);
            cseq_notedv(layer, PITCH_C4, length, 50);
            cseq_section_end(layer);

        } else if (info->channelType == AUDIOAPI_CHANNEL_TYPE_STEREO) {
            layer = cseq_layer_create(root);
            cseq_ldlayer(chan, 0, layer);
            cseq_instr(layer, channelNo * 2);
            cseq_notepan(layer, 0);
            cseq_notedv(layer, PITCH_C4, length, 50);
            cseq_section_end(layer);

            layer = cseq_layer_create(root);
            cseq_ldlayer(chan, 1, layer);
            cseq_instr(layer, channelNo * 2 + 1);
            cseq_notepan(layer, 127);
            cseq_notedv(layer, PITCH_C4, length, 50);
            cseq_section_end(layer);
        }

        cseq_delay(chan, length);
        cseq_section_end(chan);
    }

    cseq_vol(seq, 0x7F);
    cseq_tempo(seq, 0x01);
    cseq_delay(seq, length - 1);

    if (info->loopCount == -1) {
        cseq_jump(seq, label);
    }

    cseq_freechan(seq, (1 << channelCount) - 1);
    cseq_section_end(seq);

    cseq_compile(root, 0);

    seqSize = root->buffer->size;
    seqData = recomp_alloc(seqSize);
    Lib_MemCpy(seqData, root->buffer->data, seqSize);

    cseq_destroy(root);

    entry = (AudioTableEntry){
        (uintptr_t)seqData,
        seqSize,
        MEDIUM_CART,
        CACHE_EITHER,
        0, 0, 0,
    };

    seqId = AudioApi_AddSequence(&entry);
    AudioApi_AddSequenceFont(seqId, fontId);

    return seqId;
}

RECOMP_EXPORT s32 AudioApi_CreateStreamedBgm(AudioApiFileInfo* info, char* dir, char* filename) {
    AudioApiFileInfo defaultInfo = {0};

    if (info == NULL) {
        info = &defaultInfo;
    }

    if (!AudioApi_AddAudioFileFromFs(info, dir, filename)) {
        return -1;
    }

    if (info->channelType == AUDIOAPI_CHANNEL_TYPE_DEFAULT) {
        info->channelType = info->trackCount & 1
            ? AUDIOAPI_CHANNEL_TYPE_MONO
            : AUDIOAPI_CHANNEL_TYPE_STEREO;
    }

    info->loopCount = -1;

    return AudioApi_CreateStreamedSequence(info);
}

RECOMP_EXPORT s32 AudioApi_CreateStreamedFanfare(AudioApiFileInfo* info, char* dir, char* filename) {
    AudioApiFileInfo defaultInfo = {0};
    s32 seqId;

    if (info == NULL) {
        info = &defaultInfo;
    }

    if (!AudioApi_AddAudioFileFromFs(info, dir, filename)) {
        return -1;
    }

    if (info->channelType == AUDIOAPI_CHANNEL_TYPE_DEFAULT) {
        info->channelType = info->trackCount & 1
            ? AUDIOAPI_CHANNEL_TYPE_MONO
            : AUDIOAPI_CHANNEL_TYPE_STEREO;
    }

    info->loopCount = 0;

    seqId = AudioApi_CreateStreamedSequence(info);
    if (seqId == -1) {
        return -1;
    }

    AudioApi_SetSequenceFlags(seqId, SEQ_FLAG_FANFARE);

    return seqId;
}

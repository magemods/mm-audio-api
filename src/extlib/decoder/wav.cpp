#include <extlib/decoder/wav.hpp>

#include <algorithm>
#include <map>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include <dr_wav.h>

namespace Decoder {

void Wav::open() {
    if (decoder != nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);
    decoder = new drwav;

    drwav_bool32 result = firstOpen
        ? drwav_init_with_metadata(decoder, Wav::onRead, Wav::onSeek, Wav::onTell, this, 0, nullptr)
        : drwav_init(decoder, Wav::onRead, Wav::onSeek, Wav::onTell, this, nullptr);

    if (!result) {
        delete decoder;
        decoder = nullptr;
        throw std::runtime_error("Decoder error: failed to open decoder");
    }
}

void Wav::close() {
    if (decoder == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);
    drwav_uninit(decoder);
    delete decoder;
    decoder = nullptr;
}

void Wav::probe() {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    metadata->setTrackCount(decoder->channels);
    metadata->setSampleRate(decoder->sampleRate);
    metadata->setSampleCount(decoder->totalPCMFrameCount);

    for (int i = 0; i < decoder->metadataCount; i++) {
        drwav_metadata& meta = decoder->pMetadata[i];

        switch (meta.type) {
        case drwav_metadata_type_cue: {
            drwav_cue& cue = meta.data.cue;
            for (int j = 0; j < cue.cuePointCount; j++) {
                drwav_cue_point& point = cue.pCuePoints[j];
                metadata->setCuePointOffset(point.id, point.sampleOffset);
            }
            break;
        }
        case drwav_metadata_type_list_label: {
            drwav_list_label_or_note& label = meta.data.labelOrNote;
            metadata->setCuePointLabel(label.cuePointId, label.pString, label.stringLength);
            break;
        }
        case drwav_metadata_type_list_labelled_cue_region: {
            drwav_list_labelled_cue_region& region = meta.data.labelledCueRegion;
            metadata->setCuePointLength(region.cuePointId, region.sampleLength);
            break;
        }
        case drwav_metadata_type_smpl: {
            if (meta.data.smpl.sampleLoopCount > 0) {
                drwav_smpl_loop& loop = meta.data.smpl.pLoops[0];
                metadata->setLoopInfo(loop.firstSampleOffset, loop.lastSampleOffset, loop.playCount);
            }
            break;
        }
        default:
            break;
        }
    }

    metadata->findLoopPoints();
}

long Wav::decode(std::vector<int16_t>* buffer, size_t count, size_t offset) {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    size_t framesToRead = std::min(count, metadata->sampleCount - offset);
    drwav_bool32 result = drwav_seek_to_pcm_frame(decoder, offset);

    if (!result) {
        throw std::runtime_error("Decoder error: failed to seek to frame");
    }

    return drwav_read_pcm_frames_s16(decoder, framesToRead, buffer->data());
}

size_t Wav::onRead(void* datasrc, void* ptr, size_t bytes) {
    auto that = static_cast<Wav*>(datasrc);
    return that->file->read(ptr, bytes);
}

drwav_bool32 Wav::onSeek(void* datasrc, int offset, drwav_seek_origin whence) {
    auto that = static_cast<Wav*>(datasrc);
    that->file->seek(offset, whence);
    return DRWAV_TRUE;
}

drwav_bool32 Wav::onTell(void* datasrc, drwav_int64* pCursor) {
    auto that = static_cast<Wav*>(datasrc);
    *pCursor = (drwav_int64)that->file->tell();
    return DRWAV_TRUE;
}

} // namespace Decoder

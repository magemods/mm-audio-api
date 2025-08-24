#include <extlib/decoder/flac.hpp>

#include <cstring>
#include <algorithm>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include <dr_flac.h>

namespace Decoder {

void Flac::open() {
    if (decoder != nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    decoder = firstOpen
        ? drflac_open_with_metadata(Flac::onRead, Flac::onSeek, Flac::onTell, Flac::onMeta, this, nullptr)
        : drflac_open(Flac::onRead, Flac::onSeek, Flac::onTell, this, nullptr);

    if (!decoder) {
        throw std::runtime_error("Decoder error: failed to open decoder");
    }

    firstOpen = false;
}

void Flac::close() {
    if (decoder == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);
    drflac_close(decoder);
    decoder = nullptr;
    pos.store(0);
}

void Flac::probe() {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    metadata->setTrackCount(decoder->channels);
    metadata->setSampleRate(decoder->sampleRate);
    metadata->setSampleCount(decoder->totalPCMFrameCount);
    metadata->findLoopPoints();
}

long Flac::decode(std::vector<int16_t>* buffer, size_t count, size_t offset) {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    size_t framesToRead = std::min(count, metadata->sampleCount - offset);

    if (pos.load() != offset) {
        if (!drflac_seek_to_pcm_frame(decoder, offset)) {
            throw std::runtime_error("Decoder error: failed to seek to frame");
        }
    }

    pos.store(offset + framesToRead);

    return drflac_read_pcm_frames_s16(decoder, framesToRead, buffer->data());
}

size_t Flac::onRead(void* datasrc, void* ptr, size_t bytes) {
    auto that = static_cast<Flac*>(datasrc);
    return that->file->read(ptr, bytes);
}

drflac_bool32 Flac::onSeek(void* datasrc, int offset, drflac_seek_origin whence) {
    auto that = static_cast<Flac*>(datasrc);
    that->file->seek(offset, whence);
    return DRFLAC_TRUE;
}

drflac_bool32 Flac::onTell(void* datasrc, drflac_int64* pCursor) {
    auto that = static_cast<Flac*>(datasrc);
    *pCursor = (drflac_int64)that->file->tell();
    return DRFLAC_TRUE;
}

void Flac::onMeta(void* datasrc, drflac_metadata* metadata) {
    auto that = static_cast<Flac*>(datasrc);

    switch (metadata->type) {
    case DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT: {
        drflac_vorbis_comment_iterator it;
        drflac_init_vorbis_comment_iterator(&it, metadata->data.vorbis_comment.commentCount,
                                            metadata->data.vorbis_comment.pComments);
        for (;;) {
            drflac_uint32 size;
            const char* comment = drflac_next_vorbis_comment(&it, &size);
            if (comment == nullptr) {
                break;
            }
            that->metadata->parseComment(comment, size);
        }
        break;
    }
    case DRFLAC_METADATA_BLOCK_TYPE_APPLICATION: {
        if (metadata->data.application.id == 0x72696666) { // RIFF chunk storage
            drflac_uint32 size = metadata->data.application.dataSize;
            const uint8_t* data = reinterpret_cast<const uint8_t*>(metadata->data.application.pData);

            if (size >= 4) {
                if (std::memcmp(data, "cue ", 4) == 0) {
                    that->metadata->parseRiffCue(data, size);
                } else if (std::memcmp(data, "LIST", 4) == 0) {
                    that->metadata->parseRiffList(data, size);
                } else if (std::memcmp(data, "smpl", 4) == 0) {
                    that->metadata->parseRiffSmpl(data, size);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

} // namespace Decoder

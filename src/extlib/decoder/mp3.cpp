#include <extlib/decoder/mp3.hpp>

#include <algorithm>
#include <map>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <dr_mp3.h>

namespace Decoder {

void Mp3::open() {
    if (decoder != nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    decoder = new drmp3;

    drmp3_bool32 result = firstOpen
        ? drmp3_init(decoder, Mp3::onRead, Mp3::onSeek, Mp3::onTell, Mp3::onMeta, this, nullptr)
        : drmp3_init(decoder, Mp3::onRead, Mp3::onSeek, Mp3::onTell, nullptr, this, nullptr);

    if (!result) {
        delete decoder;
        decoder = nullptr;
        throw std::runtime_error("Decoder error: failed to open decoder");
    }

    firstOpen = false;
}

void Mp3::close() {
    if (decoder == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);
    drmp3_uninit(decoder);
    delete decoder;
    decoder = nullptr;
    pos.store(0);
}

void Mp3::probe() {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    metadata->setTrackCount(decoder->channels);
    metadata->setSampleRate(decoder->sampleRate);
    metadata->setSampleCount(decoder->totalPCMFrameCount);
    metadata->findLoopPoints();
}

long Mp3::decode(std::vector<int16_t>* buffer, size_t count, size_t offset) {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    size_t framesToRead = std::min(count, metadata->sampleCount - offset);

    if (pos.load() != offset) {
        if (!drmp3_seek_to_pcm_frame(decoder, offset)) {
            throw std::runtime_error("Decoder error: failed to seek to frame");
        }
    }

    pos.store(offset + framesToRead);

    return drmp3_read_pcm_frames_s16(decoder, framesToRead, buffer->data());
}

size_t Mp3::onRead(void* datasrc, void* ptr, size_t bytes) {
    auto that = static_cast<Mp3*>(datasrc);
    return that->file->read(ptr, bytes);
}

drmp3_bool32 Mp3::onSeek(void* datasrc, int offset, drmp3_seek_origin whence) {
    auto that = static_cast<Mp3*>(datasrc);
    that->file->seek(offset, whence);
    return DRMP3_TRUE;
}

drmp3_bool32 Mp3::onTell(void* datasrc, drmp3_int64* pCursor) {
    auto that = static_cast<Mp3*>(datasrc);
    *pCursor = (drmp3_int64)that->file->tell();
    return DRMP3_TRUE;
}

void Mp3::onMeta(void* datasrc, const drmp3_metadata* metadata) {
    auto that = static_cast<Mp3*>(datasrc);

    switch (metadata->type) {
    case DRMP3_METADATA_TYPE_APE:
        that->metadata->parseComment((const char*)metadata->pRawData, metadata->rawDataSize);
        break;
    case DRMP3_METADATA_TYPE_ID3V1:
        that->metadata->parseId3v1((const uint8_t*)metadata->pRawData, metadata->rawDataSize);
        break;
    case DRMP3_METADATA_TYPE_ID3V2:
        that->metadata->parseId3v2((const uint8_t*)metadata->pRawData, metadata->rawDataSize);
        break;
    default:
        break;
    }
}

} // namespace Decoder

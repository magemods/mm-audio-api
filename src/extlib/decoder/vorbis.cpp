#include <extlib/decoder/vorbis.hpp>
#include <algorithm>

namespace Decoder {

static const ov_callbacks callbacks = {
    Vorbis::onRead,
    Vorbis::onSeek,
    Vorbis::onClose,
    Vorbis::onTell,
};

void Vorbis::open() {
    if (decoder != nullptr) {
        return;
    }

    decoder = new OggVorbis_File;
    int result = ov_open_callbacks(this, decoder, nullptr, 0, callbacks);

    if (result != 0) {
        delete decoder;
        decoder = nullptr;
        throw std::runtime_error("Decoder error: failed to open decoder");
    }

    bitstream = 0;
}

void Vorbis::close() {
    if (decoder == nullptr) {
        return;
    }
    ov_clear(decoder);
    delete decoder;
    decoder = nullptr;
    pos.store(0);
}

void Vorbis::probe() {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    vorbis_info* vi = ov_info(decoder, -1);
    if (vi == nullptr) {
        throw std::runtime_error("Decoder error: failed to read info");
    }

    metadata->setTrackCount(vi->channels);
    metadata->setSampleRate(vi->rate);
    metadata->setSampleCount(ov_pcm_total(decoder, -1));

    vorbis_comment* tags = ov_comment(decoder, -1);
    if (tags) {
        for (int i = 0; i < tags->comments; i++) {
            metadata->parseComment(tags->user_comments[i], tags->comment_lengths[i]);
        }
    }

    metadata->findLoopPoints();
}

long Vorbis::decode(std::vector<int16_t>* buffer, size_t count, size_t offset) {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    int16_t* ptr = buffer->data();
    size_t framesToRead = std::min(count, metadata->sampleCount - offset);
    size_t framesRead = 0;
    long result = 0;

    if (pos.load() != offset) {
        if (ov_pcm_seek(decoder, offset) != 0) {
            throw std::runtime_error("Decoder error: failed to seek to frame");
        }
    }


    while (framesRead < framesToRead) {
        result = ov_read(decoder, reinterpret_cast<char*>(ptr + framesRead * metadata->trackCount),
                         (framesToRead - framesRead) * metadata->trackCount * sizeof(int16_t), 0, 2, 1, &bitstream);

        if (result <= 0) {
            break;
        }

        framesRead += result / (metadata->trackCount * sizeof(int16_t));
    }

    if (result < 0) {
        switch (result) {
        case OV_HOLE:
            throw std::runtime_error("Decoder error: OV_HOLE");
        case OV_EBADLINK:
            throw std::runtime_error("Decoder error: OV_EBADLINK");
        case OV_EINVAL:
            throw std::runtime_error("Decoder error: OV_EINVAL");
        default:
            throw std::runtime_error("Decoder error: Unknown");
        }
    }

    pos.store(offset + framesToRead);

    return framesRead;
}

size_t Vorbis::onRead(void* ptr, size_t size, size_t nmemb, void* datasrc) {
    auto that = static_cast<Vorbis*>(datasrc);
    size_t bytesRead = that->file->read(ptr, size * nmemb);
    return bytesRead / size;
}

int Vorbis::onSeek(void* datasrc, ogg_int64_t offset, int whence) {
    auto that = static_cast<Vorbis*>(datasrc);
    that->file->seek(offset, whence);
    return 0;
}

int Vorbis::onClose(void* datasrc) {
    return 0;
}

long Vorbis::onTell(void* datasrc) {
    auto that = static_cast<Vorbis*>(datasrc);
    return that->file->tell();
}

} // namespace Decoder

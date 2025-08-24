#include <extlib/decoder/opus.hpp>
#include <algorithm>

namespace Decoder {

static const OpusFileCallbacks callbacks = {
    Opus::onRead,
    Opus::onSeek,
    Opus::onTell,
    Opus::onClose
};

void Opus::open() {
    if (decoder != nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    int result = 0;
    decoder = op_open_callbacks(this, &callbacks, nullptr, 0, &result);

    if (result != 0) {
        decoder = nullptr;
        throw std::runtime_error("Decoder error: failed to open decoder");
    }
}

void Opus::close() {
    if (decoder == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);
    op_free(decoder);
    decoder = nullptr;
    pos.store(0);
}

void Opus::probe() {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    const OpusHead* head = op_head(decoder, -1);
    if (head == nullptr) {
        throw std::runtime_error("Decoder error: failed to read head");
    }

    metadata->setTrackCount(head->channel_count);
    metadata->setSampleRate(48000);
    metadata->setSampleCount(op_pcm_total(decoder, -1));

    const OpusTags* tags = op_tags(decoder, -1);
    if (tags) {
        for (int i = 0; i < tags->comments; i++) {
            metadata->parseComment(tags->user_comments[i], tags->comment_lengths[i]);
        }
    }

    metadata->findLoopPoints();
}

long Opus::decode(std::vector<int16_t>* buffer, size_t count, size_t offset) {
    if (decoder == nullptr) {
        throw std::runtime_error("Decoder error: not open");
    }

    std::unique_lock<std::mutex> lock(mutex);

    int16_t* ptr = buffer->data();
    size_t framesToRead = std::min(count, metadata->sampleCount - offset);
    size_t framesRead = 0;
    int result = 0;

    if (pos.load() != offset) {
        if (op_pcm_seek(decoder, offset) != 0) {
            throw std::runtime_error("Decoder error: failed to seek to frame");
        }
    }

    while (framesRead < framesToRead) {
        result = op_read(decoder, ptr + framesRead * metadata->trackCount,
                         (framesToRead - framesRead) * metadata->trackCount, nullptr);

        if (result <= 0) {
            break;
        }

        framesRead += result;
    }

    if (result < 0) {
        switch (result) {
        case OP_HOLE:
            throw std::runtime_error("Decoder error: OP_HOLE");
        case OP_EREAD:
            throw std::runtime_error("Decoder error: OP_EREAD");
        case OP_EFAULT:
            throw std::runtime_error("Decoder error: OP_EFAULT");
        case OP_EIMPL:
            throw std::runtime_error("Decoder error: OP_EIMPL");
        case OP_EINVAL:
            throw std::runtime_error("Decoder error: OP_EINVAL");
        case OP_ENOTFORMAT:
            throw std::runtime_error("Decoder error: OP_ENOTFORMAT");
        case OP_EBADHEADER:
            throw std::runtime_error("Decoder error: OP_EBADHEADER");
        case OP_EVERSION:
            throw std::runtime_error("Decoder error: OP_EVERSION");
        case OP_EBADPACKET:
            throw std::runtime_error("Decoder error: OP_EBADPACKET");
        case OP_EBADLINK:
            throw std::runtime_error("Decoder error: OP_EBADLINK");
        case OP_EBADTIMESTAMP:
            throw std::runtime_error("Decoder error: OP_EBADTIMESTAMP");
        default:
            throw std::runtime_error("Decoder error: Unknown");
        }
    }

    pos.store(offset + framesToRead);

    return framesRead;
}

int Opus::onRead(void* datasrc, unsigned char* ptr, int bytes) {
    auto that = static_cast<Opus*>(datasrc);
    return that->file->read(ptr, bytes);
}

int Opus::onSeek(void* datasrc, opus_int64 offset, int whence) {
    auto that = static_cast<Opus*>(datasrc);
    that->file->seek(offset, whence);
    return 0;
}

int Opus::onClose(void* datasrc) {
    return 0;
}

opus_int64 Opus::onTell(void* datasrc) {
    auto that = static_cast<Opus*>(datasrc);
    return that->file->tell();
}

} // namespace Decoder

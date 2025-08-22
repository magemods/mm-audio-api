#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <audio_api/types.h>

#include <extlib/decoder/metadata.hpp>
#include <extlib/vfs/file.hpp>

namespace Decoder {

enum class Type {
    Auto    = AUDIOAPI_CODEC_AUTO,
    Wav     = AUDIOAPI_CODEC_WAV,
    Flac    = AUDIOAPI_CODEC_FLAC,
    Vorbis  = AUDIOAPI_CODEC_VORBIS,
    Opus    = AUDIOAPI_CODEC_OPUS,
};

inline Type parseType(uint32_t val) {
    auto type = static_cast<Type>(val);
    switch(type) {
    case Type::Auto:
    case Type::Wav:
    case Type::Flac:
    case Type::Vorbis:
    case Type::Opus:
        return type;
    default:
        return Type::Auto;
    }
}

class Abstract {
public:
    Abstract() = delete;
    virtual ~Abstract() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual void probe() = 0;
    virtual long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) = 0;

    std::shared_ptr<Metadata> metadata;

protected:
    Abstract(std::shared_ptr<Vfs::File> file)
        : file(file), metadata(std::make_shared<Metadata>()) {}

    bool firstOpen = true;
    std::mutex mutex;
    std::shared_ptr<Vfs::File> file;
};

std::unique_ptr<Abstract> factory(std::shared_ptr<Vfs::File> file, Type type = Type::Auto);

} // namespace Decoder

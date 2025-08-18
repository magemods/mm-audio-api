#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <extlib/decoder/metadata.hpp>
#include <extlib/vfs/file.hpp>

namespace Decoder {

enum class Type { UNK, WAV, FLAC, VORBIS, OPUS };

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

    std::mutex mutex;
    std::shared_ptr<Vfs::File> file;
    bool firstOpen = true;
};

std::unique_ptr<Abstract> factory(std::shared_ptr<Vfs::File> file, Type type = Type::UNK);

} // namespace Decoder

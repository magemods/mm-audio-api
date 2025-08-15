#include "abstract.hpp"

#include <cstring>
#include <array>
#include <string>
#include <utility>

#include <ogg/ogg.h>

#include "wav.hpp"
#include "flac.hpp"
#include "vorbis.hpp"
#include "opus.hpp"

namespace Decoder {

constexpr size_t OGG_BUFFER_SIZE = 4096;

const std::array<std::pair<std::string, Type>, 3> oggHeaderTypes = {
    {
        {"\x01vorbis", Type::VORBIS},
        {"OpusHead",   Type::OPUS},
        {"\x7F""FLAC", Type::FLAC}
    }
};

Type getOggType(std::shared_ptr<Vfs::File> file) {
    ogg_sync_state oy{};
    ogg_stream_state os{};
    ogg_page og;
    ogg_packet op;
    char* buffer;
    size_t bytesRead;

    Type type = Type::UNK;

    try {
        file->open();

        ogg_sync_init(&oy);
        buffer = ogg_sync_buffer(&oy, OGG_BUFFER_SIZE);

        file->seek(0, SEEK_SET);
        bytesRead = file->read(static_cast<void*>(buffer), OGG_BUFFER_SIZE);

        ogg_sync_wrote(&oy, bytesRead);

        if (ogg_sync_pageout(&oy, &og) != 1) {
            throw std::runtime_error("error writing page");
        }
        if (ogg_stream_init(&os, ogg_page_serialno(&og)) != 0) {
            throw std::runtime_error("error init stream");
        }
        if (ogg_stream_pagein(&os, &og) != 0) {
            throw std::runtime_error("error reading page");
        }
        if (ogg_stream_packetout(&os, &op) != 1) {
            throw std::runtime_error("error writing packet");
        }

        for (const auto& [ headerStr, headerType ] : oggHeaderTypes) {
            if (std::memcmp(reinterpret_cast<char*>(op.packet), headerStr.c_str(), headerStr.size()) == 0) {
                type = headerType;
                break;
            }
        }

        ogg_stream_clear(&os);
        ogg_sync_clear(&oy);
        file->seek(0, SEEK_SET);
        return type;

    } catch (const std::runtime_error& e) {
        ogg_stream_clear(&os);
        ogg_sync_clear(&oy);
        file->seek(0, SEEK_SET);
        throw std::runtime_error("Decoder error: could not detect ogg type (" + std::string(e.what()) + ")");
    }
}

std::unique_ptr<Abstract> factory(std::shared_ptr<Vfs::File> file, Type type) {
    if (type == Type::UNK) {
        std::string ext = file->extension();

        if (ext == ".wav" || ext == ".aiff") {
            type = Type::WAV;
        } else if (ext == ".flac") {
            type = Type::FLAC;
        } else if (ext == ".opus") {
            type = Type::OPUS;
        } else if (ext == ".ogg") {
            type = getOggType(file);
        } else {
            throw std::runtime_error("Decoder error: unknown file extension: " + ext);
        }
    }

    switch (type) {
    case Type::WAV:
        return std::make_unique<Wav>(file);
    case Type::FLAC:
        return std::make_unique<Flac>(file);
    case Type::VORBIS:
        return std::make_unique<Vorbis>(file);
    case Type::OPUS:
        return std::make_unique<Opus>(file);
    default:
        throw std::runtime_error("Decoder error: uknown decoder type");
    }
}

} // namespace Decoder

#pragma once
#include <vorbis/vorbisfile.h>
#include "abstract.hpp"

namespace Decoder {

class Vorbis : public Abstract {
public:
    Vorbis(std::shared_ptr<Vfs::File> file) : Abstract(file) {};
    ~Vorbis() { close(); };

    void open() override;
    void close() override;
    void probe() override;
    long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) override;

    static size_t onRead(void* ptr, size_t size, size_t nmemb, void* datasource);
    static int onSeek(void* datasrc, ogg_int64_t offset, int whence);
    static int onClose(void* datasrc);
    static long onTell(void* datasrc);

private:
    OggVorbis_File* decoder = nullptr;
    int bitstream = 0;
};

} // namespace Decoder

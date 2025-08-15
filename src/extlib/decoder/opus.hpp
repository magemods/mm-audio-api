#pragma once
#include <opusfile.h>
#include "abstract.hpp"

namespace Decoder {

class Opus : public Abstract {
public:
    Opus(std::shared_ptr<Vfs::File> file) : Abstract(file) {};
    ~Opus() { close(); };

    void open() override;
    void close() override;
    void probe() override;
    long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) override;

    static int onRead(void* datasrc, unsigned char* ptr, int bytes);
    static int onSeek(void* datasrc, opus_int64 offset, int whence);
    static int onClose(void* datasrc);
    static opus_int64 onTell(void* datasrc);

private:
    OggOpusFile* decoder = nullptr;
    int bitstream = 0;
};

} // namespace Decoder

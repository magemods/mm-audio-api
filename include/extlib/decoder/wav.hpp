#pragma once
#include <dr_wav.h>
#include <extlib/decoder/abstract.hpp>

namespace Decoder {

class Wav : public Abstract {
public:
    Wav(std::shared_ptr<Vfs::File> file) : Abstract(file) {};
    ~Wav() { close(); };

    void open() override;
    void close() override;
    void probe() override;
    long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) override;

    static size_t onRead(void* datasrc, void* ptr, size_t bytes);
    static drwav_bool32 onSeek(void* datasrc, int offset, drwav_seek_origin whence);
    static drwav_bool32 onTell(void* datasrc, drwav_int64* pCursor);
    static void onMeta(void* datasrc, drwav_metadata* metadata);

private:
    drwav* decoder = nullptr;
};

} // namespace Decoder

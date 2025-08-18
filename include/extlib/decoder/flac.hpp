#pragma once
#include <dr_flac.h>
#include <extlib/decoder/abstract.hpp>

namespace Decoder {

class Flac : public Abstract {
public:
    Flac(std::shared_ptr<Vfs::File> file) : Abstract(file) {};
    ~Flac() { close(); };

    void open() override;
    void close() override;
    void probe() override;
    long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) override;

    static size_t onRead(void* datasrc, void* ptr, size_t bytes);
    static drflac_bool32 onSeek(void* datasrc, int offset, drflac_seek_origin whence);
    static drflac_bool32 onTell(void* datasrc, drflac_int64* pCursor);
    static void onMeta(void* datasrc, drflac_metadata* metadata);

private:
    drflac* decoder = nullptr;
};

} // namespace Decoder

#pragma once
#include <dr_mp3.h>
#include <extlib/decoder/abstract.hpp>

namespace Decoder {

class Mp3 : public Abstract {
public:
    Mp3(std::shared_ptr<Vfs::File> file) : Abstract(file) {};
    ~Mp3() { close(); };

    void open() override;
    void close() override;
    void probe() override;
    long decode(std::vector<int16_t>* buffer, size_t count, size_t offset) override;

    static size_t onRead(void* datasrc, void* ptr, size_t bytes);
    static drmp3_bool32 onSeek(void* datasrc, int offset, drmp3_seek_origin whence);
    static drmp3_bool32 onTell(void* datasrc, drmp3_int64* pCursor);
    static void onMeta(void* datasrc, const drmp3_metadata* metadata);

private:
    drmp3* decoder = nullptr;
};

} // namespace Decoder

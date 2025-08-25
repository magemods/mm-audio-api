#include <extlib/vfs/rom_file.hpp>

namespace Vfs {

RomFile::RomFile(std::shared_ptr<Rom> rom, size_t vromOffset, size_t filesize)
    : File(), rom(rom), vromOffset(vromOffset) {
    this->filesize = filesize;
}

RomFile::~RomFile() {
}

void RomFile::open() {
}

void RomFile::close() {
    curPos = 0;
}

size_t RomFile::read(void* ptr, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    size_t bytesToRead, bytesRead;

    bytesRead = rom->extractBytesToBuffer(ptr, bytesToRead, vromOffset + curPos);
    curPos += bytesRead;

    return bytesRead;
}

int64_t RomFile::seek(int64_t offset, int whence) {
    std::lock_guard<std::mutex> lock(mutex);

    int64_t origin;
    switch (whence) {
    case SEEK_SET: origin = 0; break;
    case SEEK_CUR: origin = curPos; break;
    case SEEK_END: origin = filesize; break;
    default:       return -1;
    }

    return curPos = std::min(std::max(origin + offset, static_cast<int64_t>(0)), static_cast<int64_t>(filesize));
}

int64_t RomFile::tell() {
    std::lock_guard<std::mutex> lock(mutex);
    return curPos;
}

} // namespace Vfs

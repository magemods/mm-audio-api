#include "zip_file.hpp"

namespace Vfs {

ZipFile::ZipFile(std::shared_ptr<ZipArchive> archive, fs::path path)
    : File(path), archive(archive) {

    info = archive->locateFile(path.string());
    filesize = info.size;
}

ZipFile::~ZipFile() {
}

void ZipFile::open() {
    if (info.compressed && buffer.size() == 0) {
        std::lock_guard<std::mutex> lock(mutex);
        archive->extractFileToBuffer(path.string(), buffer);
    }
}

void ZipFile::close() {
    if (info.compressed) {
        std::lock_guard<std::mutex> lock(mutex);
        buffer.resize(0);
    }
    curPos = 0;
}

size_t ZipFile::read(void* ptr, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    size_t bytesToRead, bytesRead;

    if (info.compressed) {
        bytesToRead = std::min(buffer.size() - curPos, bytes);
        bytesRead = bytesToRead;
        std::copy(buffer.data() + curPos, buffer.data() + curPos + bytesToRead, static_cast<uint8_t*>(ptr));
    } else {
        bytesToRead = std::min(filesize - curPos, bytes);
        bytesRead = archive->extractBytesToBuffer(ptr, bytesToRead, info.offset + curPos);
    }

    curPos += bytesRead;
    return bytesRead;
}

int64_t ZipFile::seek(int64_t offset, int whence) {
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

int64_t ZipFile::tell() {
    std::lock_guard<std::mutex> lock(mutex);
    return curPos;
}

} // namespace Vfs

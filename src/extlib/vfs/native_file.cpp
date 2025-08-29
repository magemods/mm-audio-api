#include <extlib/vfs/native_file.hpp>

namespace Vfs {

NativeFile::NativeFile(fs::path path)
    : File(path), stream(path, std::ios::binary | std::ios::ate) {

    stream.exceptions(std::ifstream::badbit);

    if (!stream.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    filesize = static_cast<size_t>(stream.tellg());
    stream.close();
}

NativeFile::~NativeFile() {
}

void NativeFile::open() {
    if (stream.is_open()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    stream.open(path, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }
}

void NativeFile::close() {
    std::lock_guard<std::mutex> lock(mutex);
    stream.close();
}

size_t NativeFile::read(void* buffer, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);

    stream.read(reinterpret_cast<char*>(buffer), bytes);

    if (stream.eof()) {
        stream.clear();
    }
    if (stream.fail()) {
        throw std::runtime_error("Read operation failed: " + path.string());
    }

    return static_cast<size_t>(stream.gcount());
}

int64_t NativeFile::seek(int64_t offset, int whence) {
    std::lock_guard<std::mutex> lock(mutex);

    std::ios_base::seekdir dir;
    switch (whence) {
    case SEEK_SET: dir = std::ios::beg; break;
    case SEEK_CUR: dir = std::ios::cur; break;
    case SEEK_END: dir = std::ios::end; break;
    default:       return -1;
    }

    stream.seekg(offset, dir);

    if (stream.eof()) {
        stream.clear();
        return filesize;
    }
    if (stream.fail()) {
        return -1;
    }

    return stream.tellg();
}

int64_t NativeFile::tell() {
    std::lock_guard<std::mutex> lock(mutex);

    int64_t pos = stream.tellg();

    if (stream.eof()) {
        stream.clear();
    }
    if (stream.fail()) {
        return -1;
    }

    return pos;
}

} // namespace Vfs

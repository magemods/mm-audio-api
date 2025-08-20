#pragma once

#include <filesystem>
#include <mutex>
#include <memory>

namespace fs = std::filesystem;

namespace Vfs {

class File {
public:
    File() = delete;
    virtual ~File() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual size_t read(void* buffer, size_t bytes) = 0;
    virtual int64_t seek(int64_t offset, int whence) = 0;
    virtual int64_t tell() = 0;

    size_t size() const {
        return filesize;
    };

    std::string fullpath() const {
        return path.string();
    };

    std::string extension() const {
        return path.extension().string();
    };

protected:
    File(fs::path path)
        : path(path) {};

    size_t filesize;
    fs::path path;
    std::mutex mutex;
};

} // namespace Vfs

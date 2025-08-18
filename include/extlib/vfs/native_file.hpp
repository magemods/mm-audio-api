#pragma once

#include <filesystem>
#include <fstream>

#include <extlib/vfs/file.hpp>

namespace fs = std::filesystem;

namespace Vfs {

class NativeFile : public File {
public:
    NativeFile() = delete;
    NativeFile(fs::path path);
    ~NativeFile();

    void open() override;
    void close() override;
    size_t read(void* buffer, size_t bytes) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t tell() override;

private:
    std::ifstream stream;
};

} // namespace Vfs

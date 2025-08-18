#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <memory>

#include <extlib/vfs/file.hpp>
#include <extlib/vfs/zip_archive.hpp>

namespace fs = std::filesystem;

namespace Vfs {

class ZipFile : public File {
public:
    ZipFile() = delete;
    ZipFile(std::shared_ptr<ZipArchive> archive, fs::path path);
    ~ZipFile();

    void open() override;
    void close() override;
    size_t read(void* buffer, size_t bytes) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t tell() override;

private:
    ZipArchive::FileInfo info;

    size_t curPos = 0;
    std::vector<uint8_t> buffer;
    std::shared_ptr<ZipArchive> archive;
};

} // namespace Vfs

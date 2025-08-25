#pragma once

#include <memory>

#include <extlib/vfs/file.hpp>
#include <extlib/vfs/rom.hpp>

namespace Vfs {

class RomFile : public File {
public:
    RomFile() = delete;
    RomFile(std::shared_ptr<Rom> rom, size_t vromOffset, size_t filesize = -1);
    ~RomFile();

    void open() override;
    void close() override;
    size_t read(void* buffer, size_t bytes) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t tell() override;

private:
    size_t vromOffset = 0;
    size_t curPos = 0;
    std::shared_ptr<Rom> rom;
};

} // namespace Vfs

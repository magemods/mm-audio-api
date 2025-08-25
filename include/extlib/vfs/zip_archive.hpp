#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Vfs {

class ZipArchive {
public:
    ZipArchive() = delete;

    static std::shared_ptr<ZipArchive> factory(fs::path path);

    struct FileInfo {
        size_t index;
        size_t size;
        size_t offset = 0;
        bool compressed = true;
    };

    FileInfo locateFile(std::string path);
    void extractFileToBuffer(std::string path, std::vector<uint8_t>& buffer);
    size_t extractBytesToBuffer(void* buffer, size_t bytes, size_t offset);

private:
    struct Private{ explicit Private() = default; };

    ZipArchive(Private, fs::path path);
    void init();

    void* mz_archive;
    size_t filesize;
    fs::path path;
    std::ifstream stream;
    std::mutex mutex;

    std::unordered_map<std::string, FileInfo> fileList;
    std::shared_mutex fileListMutex;

    static std::unordered_map<fs::path, std::shared_ptr<ZipArchive>> cache;
    static std::shared_mutex cacheMutex;
    static std::shared_ptr<ZipArchive> checkCache(fs::path path);
    static size_t onRead(void* datasrc, uint64_t offset, void* buffer, size_t bytes);

};

} // namespace Vfs

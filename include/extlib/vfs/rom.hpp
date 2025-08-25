#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <audio_api/types.h>
#include <extlib/utils.hpp>

namespace fs = std::filesystem;

using RomDesc = AudioApiRomDesc;

namespace Vfs {

class Rom {
public:
    Rom() = delete;

    static std::shared_ptr<Rom> factory(fs::path path);
    static void addRomDesc(RomDesc* romDesc);

    size_t extractBytesToBuffer(void* buffer, size_t bytes, size_t offset);

private:
    struct Private{ explicit Private() = default; };

    Rom(Private, fs::path path);
    void init();

    RomDesc romDesc;
    size_t filesize;
    fs::path path;
    std::ifstream stream;
    std::mutex mutex;

    struct DmaEntry {
        std::pair<uint32_t, uint32_t> virtualAddr;
        std::pair<uint32_t, uint32_t> physicalAddr;
        std::vector<uint8_t> uncompressedData;

        DmaEntry(uint8_t* buf) {
            virtualAddr.first   = read_u32_be(buf);
            virtualAddr.second  = read_u32_be(buf + sizeof(uint32_t));
            physicalAddr.first  = read_u32_be(buf + sizeof(uint32_t) * 2);
            physicalAddr.second = read_u32_be(buf + sizeof(uint32_t) * 3);
        }

        const bool isCompressed() {
            return physicalAddr.second != 0;
        }

        const bool isExist() {
            return physicalAddr.first != 0xFFFFFFFF;
        }

        const size_t getUncompressedSize() {
            if (virtualAddr.first > virtualAddr.second) {
                return 0;
            }

            return virtualAddr.second - virtualAddr.first;
        }

        const size_t getCompressedSize() {
            if (!isCompressed()) {
                return 0;
            }

            if (physicalAddr.first > physicalAddr.second) {
                return 0;
            }

            return physicalAddr.second - physicalAddr.first;
        }
    };

    std::vector<DmaEntry> dmaEntryList;

    static std::vector<RomDesc> romDescriptions;
    static std::unordered_map<fs::path, std::shared_ptr<Rom>> cache;
    static std::shared_mutex cacheMutex;
    static std::shared_ptr<Rom> checkCache(fs::path path);
};

} // namespace Vfs

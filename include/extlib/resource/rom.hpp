#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <extlib/resource/abstract.hpp>
#include <extlib/utils.hpp>
#include <extlib/vfs/file.hpp>

namespace Resource {

struct RomDesc {
    std::string gameId;
    std::string version;
    unsigned gSequenceTableOffset;
    unsigned gSoundFontTableOffset;
    unsigned gSampleBankTableOffset;
};

class Rom : public Abstract {
public:
    Rom() = delete;
    Rom(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy = CacheStrategy::Default);

    ~Rom();

    void open();
    void close();

    std::vector<uint8_t> read(size_t offset, size_t size);

    size_t size() const {
        return file->size();
    };

    void dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t size, uint32_t arg1, uint32_t arg2) override;
    std::vector<PreloadTask> getPreloadTasks() override;
    void runPreloadTask(const PreloadTask& task) override;
    void gc() override;

    static void addRomDesc(std::string code, RomDesc romDesc);

protected:
    std::shared_ptr<Vfs::File> file;
    std::atomic<std::chrono::steady_clock::time_point> atime{EPOCH};

    CacheStrategy cacheStrategy;
    std::vector<uint8_t> cache;
    std::shared_mutex cacheMutex;

    inline static std::unordered_map<std::string, std::pair<std::string, unsigned>> romDescriptions = {
        {"ad69c91157f6705e8ab06c79fe08aad47bb57ba7", {"OOT", "NTSC 1.0 (US)", 0x7430}},
        {"d3ecb253776cd847a5aa63d859d8c89a2f37b364", {"OOT", "NTSC 1.1 (US)", 0x7430}},
        {"41b3bdc48d98c48529219919015a1af22f5057c2", {"OOT", "NTSC 1.2 (US)", 0x7960}},
        {"c892bbda3993e66bd0d56a10ecd30b1ee612210f", {"OOT", "NTSC 1.0 (JP)", 0x7430}},
        {"dbfc81f655187dc6fefd93fa6798face770d579d", {"OOT", "NTSC 1.1 (JP)", 0x7430}},
        {"fa5f5942b27480d60243c2d52c0e93e26b9e6b86", {"OOT", "NTSC 1.2 (JP)", 0x7960}},
    };
};

} // namespace Resource

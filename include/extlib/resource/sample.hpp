#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <shared_mutex>
#include <vector>

#include <extlib/decoder/abstract.hpp>
#include <extlib/resource/abstract.hpp>
#include <extlib/utils.hpp>
#include <extlib/vfs/file.hpp>

namespace Resource {

class Sample : public Abstract {
public:
    Sample() = delete;
    Sample(std::shared_ptr<Vfs::File> file, Decoder::Type type = Decoder::Type::UNK,
           CacheStrategy cacheStrategy = RESOURCE_CACHE_PRELOAD_ON_USE);
    ~Sample();

    void open();
    void close();
    void probe();

    std::shared_ptr<std::vector<int16_t>> getChunk(size_t offset);

    void dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t count, uint32_t trackNo, uint32_t arg2) override;
    std::vector<PreloadTask> getPreloadTasks() override;
    void runPreloadTask(const PreloadTask& task) override;
    void gc() override;

    std::shared_ptr<Decoder::Metadata> metadata;

private:
    std::shared_ptr<Vfs::File> file;
    std::unique_ptr<Decoder::Abstract> decoder;

    size_t numChunks = 0;
    std::atomic<size_t> pos = 0;
    std::atomic<std::chrono::steady_clock::time_point> atime{EPOCH};

    CacheStrategy cacheStrategy;
    std::map<size_t, std::shared_ptr<std::vector<int16_t>>> cache;
    std::shared_mutex cacheMutex;
};

} // namespace Resource

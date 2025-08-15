#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "resource/abstract.hpp"
#include "util.h"
#include "vfs/file.hpp"

namespace Resource {

class Basic : public Abstract {
public:
    Basic() = delete;
    Basic(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy = RESOURCE_CACHE_PRELOAD_ON_USE);

    ~Basic();

    void open();
    void close();

    std::vector<uint8_t> read(size_t offset, size_t size);

    void dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t size, uint32_t arg1, uint32_t arg2) override;
    std::vector<PreloadTask> getPreloadTasks() override;
    void runPreloadTask(const PreloadTask& task) override;
    void gc() override;

private:
    std::shared_ptr<Vfs::File> file;
    std::atomic<std::chrono::steady_clock::time_point> atime{EPOCH};

    CacheStrategy cacheStrategy;
    std::vector<uint8_t> cache;
    std::shared_mutex cacheMutex;
};

} // namespace Resource

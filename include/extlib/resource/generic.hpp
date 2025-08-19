#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <vector>

#include <extlib/resource/abstract.hpp>
#include <extlib/utils.hpp>
#include <extlib/vfs/file.hpp>

namespace Resource {

class Generic : public Abstract {
public:
    Generic() = delete;
    Generic(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy = CacheStrategy::Default);

    ~Generic();

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

private:
    std::shared_ptr<Vfs::File> file;
    std::atomic<std::chrono::steady_clock::time_point> atime{EPOCH};

    CacheStrategy cacheStrategy;
    std::vector<uint8_t> cache;
    std::shared_mutex cacheMutex;
};

} // namespace Resource

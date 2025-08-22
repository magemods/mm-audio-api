#pragma once

#include <any>
#include <memory>
#include <vector>

#include <audio_api/types.h>

namespace Resource {

enum class CacheStrategy {
    Default             = AUDIOAPI_CACHE_DEFAULT,
    None                = AUDIOAPI_CACHE_NONE,
    Preload             = AUDIOAPI_CACHE_PRELOAD,
    PreloadOnUse        = AUDIOAPI_CACHE_PRELOAD_ON_USE,
    PreloadOnUseNoEvict = AUDIOAPI_CACHE_PRELOAD_ON_USE_NO_EVICT,
};

inline CacheStrategy parseCacheStrategy(uint32_t val) {
    auto cacheStrategy = static_cast<CacheStrategy>(val);
    switch(cacheStrategy) {
    case CacheStrategy::Default:
    case CacheStrategy::None:
    case CacheStrategy::Preload:
    case CacheStrategy::PreloadOnUse:
    case CacheStrategy::PreloadOnUseNoEvict:
        return cacheStrategy;
    default:
        return CacheStrategy::Default;
    }
}

struct PreloadTask {
    int priority;
    std::any data;
};

class Abstract {
public:
    virtual void dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t count, uint32_t arg1, uint32_t arg2) = 0;
    virtual std::vector<PreloadTask> getPreloadTasks() = 0;
    virtual void runPreloadTask(const PreloadTask& task) = 0;
    virtual void gc() = 0;

protected:
    bool initialPreload = true;
};

using ResourcePtr = std::shared_ptr<Abstract>;

} // namespace Resource

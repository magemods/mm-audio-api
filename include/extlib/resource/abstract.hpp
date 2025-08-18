#pragma once

#include <any>
#include <memory>
#include <vector>

#include <extlib/bridge.h>

namespace Resource {

using CacheStrategy = ResourceCacheStrategy;

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
};

using ResourcePtr = std::shared_ptr<Abstract>;

} // namespace Resource

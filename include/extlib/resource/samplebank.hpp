#pragma once

#include <memory>

#include <extlib/resource/generic.hpp>
#include <extlib/vfs/file.hpp>

namespace Resource {

class SampleBank : public Generic {
public:
    SampleBank() = delete;
    SampleBank(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy = CacheStrategy::Default);

    ~SampleBank();

    void dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t size, uint32_t devAddr, uint32_t arg2) override;
};

} // namespace Resource

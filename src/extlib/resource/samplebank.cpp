#include <extlib/resource/samplebank.hpp>

#include <mod_recomp.h>

namespace Resource {

SampleBank::SampleBank(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy)
    : Generic(file, cacheStrategy) {

    if (cacheStrategy == CacheStrategy::Default) {
        cacheStrategy = CacheStrategy::PreloadOnUse;
    }
}

SampleBank::~SampleBank() {
    close();
}

void SampleBank::dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t size, uint32_t devAddr, uint32_t arg2) {
    {
        std::shared_lock cacheLock(cacheMutex);

        if (offset + devAddr + size <= cache.size()) {
            for (size_t i = 0; i < size; i++) {
                MEM_B(ptr, i) = cache[offset + devAddr + i];
            }

            return;
        }
    }

    std::vector<uint8_t> buffer = read(offset + devAddr, size);

    for (size_t i = 0; i < size; i++) {
        MEM_B(ptr, i) = buffer[i];
    }

    if (cacheStrategy != CacheStrategy::None && offset == 0 && size >= file->size()) {
        std::unique_lock cacheLock(cacheMutex);
        cache = std::move(buffer);
    }
}

} // namespace Resource

#include <extlib/resource/basic.hpp>

#include <mod_recomp.h>

namespace Resource {

constexpr int FILE_TTL_SECONDS = 30;

Basic::Basic(std::shared_ptr<Vfs::File> file, CacheStrategy cacheStrategy)
    : file(file), cacheStrategy(cacheStrategy) {

    if (cacheStrategy == CacheStrategy::Default) {
        cacheStrategy = CacheStrategy::PreloadOnUse;
    }
}

Basic::~Basic() {
    close();
}

void Basic::open() {
    file->open();
    atime.store(std::chrono::steady_clock::now());
}

void Basic::close() {
    file->close();
    atime.store(EPOCH);
}

std::vector<uint8_t> Basic::read(size_t offset, size_t size) {
    std::vector<uint8_t> buffer(size);

    open();
    file->seek(offset, SEEK_SET);
    file->read(buffer.data(), size);

    return buffer;
}

void Basic::dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t size, uint32_t arg1, uint32_t arg2) {
    {
        std::shared_lock cacheLock(cacheMutex);

        if (offset + size <= cache.size()) {
            for (size_t i = 0; i < size; i++) {
                MEM_B(ptr, i) = cache[offset + i];
            }
        }

        return;
    }

    std::vector<uint8_t> buffer = read(offset, size);

    for (size_t i = 0; i < size; i++) {
        MEM_B(ptr, i) = buffer[i];
    }

    if (cacheStrategy != CacheStrategy::None && offset == 0 && size == file->size()) {
        std::unique_lock cacheLock(cacheMutex);
        cache = std::move(buffer);
    }
}

std::vector<PreloadTask> Basic::getPreloadTasks() {
    static bool initialPreload = true;

    if (cacheStrategy == CacheStrategy::None) {
        return {};
    }

    if (initialPreload == true) {
        initialPreload = false;
        if (cacheStrategy == CacheStrategy::Preload) {
            return {{ 0, true }};
        }
    } else if (cacheStrategy == CacheStrategy::PreloadOnUse ||
               cacheStrategy == CacheStrategy::PreloadOnUseNoEvict) {
        std::shared_lock cacheLock(cacheMutex);
        if (cache.size() == 0) {
            return {{ 0, true }};
        }
    }

    return {};
}

void Basic::runPreloadTask(const PreloadTask& task) {
    std::unique_lock cacheLock(cacheMutex);
    cache = std::move(read(0, file->size()));
}

void Basic::gc() {
    auto atime = this->atime.load();
    if (atime == EPOCH) {
        return;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - atime);
    if (elapsed.count() > FILE_TTL_SECONDS) {
        if (cacheStrategy == CacheStrategy::PreloadOnUse) {
            cache.resize(0);
        }
        return close();
    }
}

} // namespace Resource

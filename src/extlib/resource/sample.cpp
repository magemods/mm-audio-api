#include <extlib/resource/sample.hpp>

#include <algorithm>

#include <mod_recomp.h>

namespace Resource {

constexpr int FILE_TTL_SECONDS = 30;
constexpr size_t CHUNK_SIZE = 1024;
constexpr int CACHE_INITIAL_CHUNKS = 8;
constexpr int CACHE_FOLLOWUP_CHUNKS = 32;

inline size_t CHUNK_START(size_t offset) {
    return (offset / CHUNK_SIZE) * CHUNK_SIZE;
}

inline size_t CHUNK_END(size_t offset) {
    return CHUNK_START(offset) + CHUNK_SIZE;
}

Sample::Sample(std::shared_ptr<Vfs::File> file, Decoder::Type type, CacheStrategy cacheStrategy)
    : file(file), cacheStrategy(cacheStrategy) {

    decoder = Decoder::factory(file, type);
    metadata = decoder->metadata;
}

Sample::~Sample() {
    close();
}

void Sample::open() {
    file->open();
    decoder->open();
    atime.store(std::chrono::steady_clock::now());
}

void Sample::close() {
    decoder->close();
    file->close();
    pos.store(0);
    atime.store(EPOCH);
}

void Sample::probe() {
    decoder->probe();
    numChunks = (metadata->sampleCount / CHUNK_SIZE) - (metadata->loopStart / CHUNK_SIZE) + 1;
}

std::shared_ptr<std::vector<int16_t>> Sample::getChunk(size_t offset) {
    try {
        std::shared_lock<std::shared_mutex> cacheLock(cacheMutex);
        return cache.at(offset);
    } catch (const std::out_of_range& e) {
    }

    open();

    size_t framesToRead = std::min(CHUNK_SIZE, metadata->sampleCount - offset - 1);
    auto buffer = std::make_shared<std::vector<int16_t>>(framesToRead * metadata->trackCount);

    size_t framesRead = decoder->decode(buffer.get(), framesToRead, offset);

    if (framesRead != framesToRead) {
        throw std::runtime_error("Not enough samples read");
    }

    std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
    cache[offset] = buffer;

    return buffer;
}


void Sample::dma(uint8_t* rdram, int32_t ptr, size_t offset, size_t count, uint32_t trackNo, uint32_t arg2) {
    if (trackNo >= metadata->trackCount) {
        throw std::invalid_argument("Invalid trackNo " + std::to_string(trackNo));
    }

    size_t chunkOffset, i;

    for (chunkOffset = CHUNK_START(offset); chunkOffset < CHUNK_END(offset + count); chunkOffset += CHUNK_SIZE) {
        if (chunkOffset >= metadata->sampleCount) {
            break;
        }

        auto chunk = getChunk(chunkOffset);
        auto data = chunk->data();

        for (i = std::max(chunkOffset, offset); i < std::min(CHUNK_END(chunkOffset), offset + count); i++) {
            MEM_H(ptr, (i - offset) * 2) = data[(i - chunkOffset) * metadata->trackCount + trackNo];
        }
    }

    pos.store(offset);
}

std::vector<PreloadTask> Sample::getPreloadTasks() {
    static bool initialPreload = true;

    if (cacheStrategy == RESOURCE_CACHE_NONE) {
        return {};
    }

    if (initialPreload == true) {
        initialPreload = false;
        return {{ 0, true }};
    }

    if (cacheStrategy == RESOURCE_CACHE_PRELOAD) {
        return {};
    }

    std::vector<PreloadTask> tasks;
    tasks.reserve(CACHE_FOLLOWUP_CHUNKS);

    for (int i = 1; i <= CACHE_FOLLOWUP_CHUNKS; i++) {
        size_t offset = CHUNK_START(pos + i * CHUNK_SIZE - 1);
        if (offset >= metadata->sampleCount) {
            offset = CHUNK_START(metadata->loopStart) + (offset - CHUNK_END(metadata->sampleCount));
        }
        tasks.emplace_back(i, offset);
    }

    return tasks;
}

void Sample::runPreloadTask(const PreloadTask& task) {
    if (task.data.type() == typeid(size_t)) {
        size_t offset = std::any_cast<size_t>(task.data);
        getChunk(offset);
        return;
    }

    size_t preloadChunks = cacheStrategy == RESOURCE_CACHE_PRELOAD
        ? numChunks
        : CACHE_INITIAL_CHUNKS;

    for (int i = 0; i < preloadChunks; i++) {
        size_t offset = CHUNK_START(i * CHUNK_SIZE);
        if (offset >= metadata->sampleCount) {
            break;
        }
        getChunk(offset);
    }

    close();
}

void Sample::gc() {
    auto atime = this->atime.load();
    if (atime == EPOCH) {
        return;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - atime);
    if (elapsed.count() > FILE_TTL_SECONDS) {
        return close();
    }

    if (cacheStrategy == RESOURCE_CACHE_NONE || cacheStrategy == RESOURCE_CACHE_PRELOAD_ON_USE) {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);

        size_t curChunk = pos.load() / CHUNK_SIZE;

        auto it = cache.begin();
        while (it != cache.end()) {
            size_t thisChunk = it->first / CHUNK_SIZE;
            size_t dist = (curChunk > thisChunk)
                ? (numChunks - (curChunk - thisChunk))
                : (thisChunk - curChunk);

            if ((thisChunk >= CACHE_INITIAL_CHUNKS) && (dist > CACHE_FOLLOWUP_CHUNKS) && (dist < numChunks - 1)) {
                it = cache.erase(it);
            } else {
                it++;
            }
        }
    }
}

} // namespace Resource

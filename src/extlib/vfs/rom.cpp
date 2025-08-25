#include <extlib/vfs/rom.hpp>

#include <extlib/lib/sha1.hpp>
#include <extlib/lib/yaz0.hpp>
#include <extlib/utils.hpp>
#include <plog/Log.h>

namespace Vfs {

constexpr int DMA_ENTRY_RESERVE_SIZE = 1024;
constexpr int SIZE_OF_YAZ0_HEADER = 0x10;

std::vector<RomDesc> Rom::romDescriptions;
std::unordered_map<fs::path, std::shared_ptr<Rom>> Rom::cache;
std::shared_mutex Rom::cacheMutex;

Rom::Rom(Rom::Private, fs::path path)
    : path(path), stream(path, std::ios::binary | std::ios::ate) {

    stream.exceptions(std::ifstream::badbit);

    if (!stream.is_open()) {
        throw std::runtime_error("Could not open rom");
    }

    filesize = static_cast<size_t>(stream.tellg());
    stream.seekg(0);
}

std::shared_ptr<Rom> Rom::checkCache(fs::path path) {
    try {
        std::shared_lock<std::shared_mutex> cacheLock(cacheMutex);
        return cache.at(path);
    } catch (const std::out_of_range& e) {
        return nullptr;
    }
}

std::shared_ptr<Rom> Rom::factory(fs::path path) {
    fs::path normalized = path.lexically_normal();

    if (auto cached = checkCache(normalized)) {
        return cached;
    }

    auto rom = std::shared_ptr<Rom>(new Rom(Private(), normalized));
    rom->init();

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        cache[path] = rom;
        // Rom::cache[path] = rom;
    }

    return rom;
}

void Rom::init() {
    SHA1 checksum;
    checksum.update(stream);
    stream.clear();

    std::string checksumStr = checksum.final();

    auto it = std::find_if(romDescriptions.begin(), romDescriptions.end(), [&](const RomDesc& romDesc) {
        //return checksumStr == romDesc.sha1hash;
        return memcmp(checksumStr.c_str(), romDesc.sha1hash, 40) == 0;
    });

    if (it == romDescriptions.end()) {
        throw std::runtime_error("ROM error: No matching romDesc");
    }

    romDesc = *it;

    uint8_t buf[0x10] = {0xFF};

    stream.seekg(romDesc.dmaTable, std::ios_base::beg);
    stream.read(reinterpret_cast<char*>(buf), 0x10);

    if (stream.fail()) {
        throw std::runtime_error("ROM error: Failed to read makerom entry");
    }

    DmaEntry makerom(buf);
    if (makerom.virtualAddr.first != 0 || makerom.physicalAddr.first != 0 || makerom.physicalAddr.second != 0) {
        throw std::runtime_error("ROM error: Invalid makerom entry");
    }

    dmaEntryList.reserve(DMA_ENTRY_RESERVE_SIZE);
    dmaEntryList.push_back(makerom);

    while (true) {
        stream.read(reinterpret_cast<char*>(buf), 0x10);
        if (stream.fail()) {
            throw std::runtime_error("ROM error: Failed to read DMA entry");
        }

        auto& entry = dmaEntryList.emplace_back(buf);
        if (entry.virtualAddr.first == 0 && entry.virtualAddr.second == 0 &&
            entry.physicalAddr.first == 0 && entry.physicalAddr.second == 0) {
            break;
        }
    }
}

void Rom::addRomDesc(RomDesc* romDesc) {
    auto copy = *romDesc;
    swap_b(copy.sha1hash, sizeof(copy.sha1hash));
    swap_b(copy.gameId, sizeof(copy.gameId));
    swap_b(copy.version, sizeof(copy.version));
    romDescriptions.push_back(copy);
}

size_t Rom::extractBytesToBuffer(void* buffer, size_t bytes, size_t vromOffset) {
    auto it = std::find_if(dmaEntryList.begin(), dmaEntryList.end(), [&](const DmaEntry& entry) {
        return entry.virtualAddr.first <= vromOffset && vromOffset < entry.virtualAddr.second;
    });

    if (it == dmaEntryList.end()) {
        throw std::runtime_error("ROM error: No matching DMA entry");
    }

    auto& entry = *it;

    if (!entry.isExist()) {
        throw std::runtime_error("ROM error: DMA entry does not exist");
    }

    std::lock_guard lock(mutex);

    size_t compressedSize = entry.getCompressedSize();
    size_t uncompressedSize = entry.getUncompressedSize();

    size_t offset = vromOffset - entry.virtualAddr.first;
    bytes = std::min(bytes, uncompressedSize - offset);

    if (entry.isCompressed()) {
        if (entry.uncompressedData.empty()) {
            if (compressedSize <= SIZE_OF_YAZ0_HEADER) {
                throw std::runtime_error("ROM error: Invalid DMA entry size");
            }

            std::vector<uint8_t> compressedData;
            compressedData.resize(compressedSize);

            stream.seekg(entry.physicalAddr.first, std::ios_base::beg);
            stream.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);

            if (stream.eof()) {
                stream.clear();
            }
            if (stream.fail()) {
                throw std::runtime_error("ROM error: Read operation failed");
            }

            entry.uncompressedData.resize(uncompressedSize);
            yaz0_decompress(uncompressedSize, compressedSize - SIZE_OF_YAZ0_HEADER,
                            compressedData.data(), entry.uncompressedData.data());
        }

        std::copy(entry.uncompressedData.begin() + offset, entry.uncompressedData.begin() + offset + bytes,
                  reinterpret_cast<uint8_t*>(buffer));

    } else {
        stream.seekg(offset, std::ios_base::beg);
        stream.read(reinterpret_cast<char*>(buffer), bytes);

        if (stream.eof()) {
            stream.clear();
        }
        if (stream.fail()) {
            throw std::runtime_error("ROM error: Read operation failed");
        }
    }

    return bytes;
}

} // namespace Vfs

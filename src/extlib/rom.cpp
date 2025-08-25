
#include "sha1.hpp"
#include "yaz0.hpp"



// AddRomResource("oot", SEQUENCE, seqId)

// DMA(resourceId, offset, size)

static const std::unordered_map<std::string, std::pair<std::string, AudioTableOffsets>> DMA_DATA_OFFSETS = {
    {"ad69c91157f6705e8ab06c79fe08aad47bb57ba7", {"OOT NTSC 1.0 (US)", {0x102AD0,0x1026A0,0x1031C0}}}, // NTSC 1.0 (US)
    {"d3ecb253776cd847a5aa63d859d8c89a2f37b364", {"OOT NTSC 1.1 (US)", {0x102C90,0x102860,0x103380}}}, // NTSC 1.1 (US)
    {"41b3bdc48d98c48529219919015a1af22f5057c2", {"OOT NTSC 1.2 (US)", {0x102B40,0x102710,0x103230}}}, // NTSC 1.2 (US)
    {"c892bbda3993e66bd0d56a10ecd30b1ee612210f", {"OOT NTSC 1.0 (JP)", {0x102AD0,0x1026A0,0x1031C0}}}, // NTSC 1.0 (JP)
    {"dbfc81f655187dc6fefd93fa6798face770d579d", {"OOT NTSC 1.1 (JP)", {0x102C90,0x102860,0x103380}}}, // NTSC 1.1 (JP)
    {"fa5f5942b27480d60243c2d52c0e93e26b9e6b86", {"OOT NTSC 1.2 (JP)", {0x102B40,0x102710,0x103230}}}, // NTSC 1.2 (JP)
};

const int ROM_LITTLE_ENDIAN = 0x40;
const int ROM_BYTESWAPPED = 0x37;

void doLE2BE(std::string &rom) {
    for (size_t i = 0; i < rom.size(); i += 4) {
        char b0 = rom[i];
        char b1 = rom[i + 1];
        char b2 = rom[i + 2];
        char b3 = rom[i + 3];

        rom[i] = b3;
        rom[i + 1] = b2;
        rom[i + 2] = b1;
        rom[i + 3] = b0;
    }
}

void doBS2BE(std::string &rom) {
    for (size_t i = 0; i < rom.size(); i += 2) {
        char tmp = rom[i];
        rom[i] = rom[i + 1];
        rom[i + 1] = tmp;
    }
}

uint32_t toU32(const unsigned char *buf) {
    uint32_t b0 = buf[0];
    uint32_t b1 = buf[1];
    uint32_t b2 = buf[2];
    uint32_t b3 = buf[3];

    return b0 << 24 | b1 << 16 | b2 << 8 | b3;
}

#define SIZE_OF_YAZ0_HEADER 0x10

struct DMADataEntry {
    std::pair<uint32_t, uint32_t> virtualAddr;
    std::pair<uint32_t, uint32_t> physicalAddr;

    const bool isCompressed() {
        return physicalAddr.second != 0;
    }

    const bool isExist() {
        return physicalAddr.first == 0xFFFFFFFF;
    }

    const size_t getUncompressedSize() {
        if (virtualAddr.first > virtualAddr.second) {
            return 0;
        }

        return virtualAddr.second - virtualAddr.first;
    }

    const size_t getCompressedSize() {
        if (!isCompressed()) {
            return 0;
        }

        if (physicalAddr.first > physicalAddr.second) {
            return 0;
        }

        return physicalAddr.second - physicalAddr.first;
    }

    DMADataEntry() : virtualAddr({0, 0}), physicalAddr({0xFFFFFFFF, 0xFFFFFFFF}) {}

    static void fillEntry(DMADataEntry &entry, const char *rom, unsigned dmaStart, unsigned index) {
        const unsigned char *entryRaw = reinterpret_cast<const unsigned char *>(rom + dmaStart + index * (sizeof(virtualAddr) + sizeof(physicalAddr)));

        entry.virtualAddr.first = toU32(entryRaw);
        entry.virtualAddr.second = toU32(entryRaw + sizeof(uint32_t));
        entry.physicalAddr.first = toU32(entryRaw + sizeof(uint32_t) * 2);
        entry.physicalAddr.second = toU32(entryRaw + sizeof(uint32_t) * 3);

        // std::cout << std::hex << "fillEntry:\ndmaStart: 0x" << dmaStart << "\nindex: 0x" << index << "\n"
        //           << "ROM Offset: 0x" << dmaStart + index * (sizeof(virtualAddr) + sizeof(physicalAddr)) << "\n"
        //           << "Virtual Address: {Start: 0x" << entry.virtualAddr.first << ", End: 0x" << entry.virtualAddr.second << "}\n"
        //           << "Physical Address: {Start: 0x"
        //           << entry.physicalAddr.first << ", End: 0x" << entry.physicalAddr.second << "}\n";
    }

    DMADataEntry(const char *rom, unsigned dmaStart, unsigned index) {
        DMADataEntry::fillEntry(*this, rom, dmaStart, index);
    }
};

static struct {
    std::vector<char> rom;
    unsigned dmaStart;
} sZ64Rom;

bool unloadOOTRom() {
    sZ64Rom.dmaStart = 0;
    sZ64Rom.rom.clear();
    sZ64Rom.rom.shrink_to_fit();
    return true;
}

bool tryLoadOOTRom() {
    if (sPMMDir == "") {
        std::cout << "Called tryLoadOOTRom before setting PMM directory!\n";
        return false;
    }

    fs::path romPathZ64 = sPMMDir / "oot.z64";
    fs::path romPathN64 = sPMMDir / "oot.n64";
    fs::path romPathV64 = sPMMDir / "oot.v64";
    fs::path *romPath = nullptr;

    if (fs::exists(romPathZ64)) {
        romPath = &romPathZ64;
    } else if (fs::exists(romPathN64)) {
        romPath = &romPathN64;
    } else if (fs::exists(romPathV64)) {
        romPath = &romPathV64;
    }

    if (romPath) {
        std::cout << "Found OoT ROM at " << romPath->string() << '\n';

        std::string rom;

        {
            std::ifstream file(*romPath, std::ios::binary);
            rom.assign(std::istreambuf_iterator{file}, {});
        }

        if (rom.size() > 0) {
            if (rom[0] == ROM_LITTLE_ENDIAN) {
                std::cout << "ROM is little endian (.v64)! Converting to big endian (.z64)...\n";
                doLE2BE(rom);
            } else if (rom[0] == ROM_BYTESWAPPED) {
                std::cout << "ROM is byte-swapped (.n64)! Converting to big endian (.z64)...\n";
                doBS2BE(rom);
            }

            SHA1 checksum;
            checksum.update(rom);

            std::string checksumStr = checksum.final();

            auto dmaIt = DMA_DATA_OFFSETS.find(checksumStr);

            if (dmaIt != DMA_DATA_OFFSETS.end()) {
                std::cout << "ROM identified as " << dmaIt->second.first << '\n';

                sZ64Rom.dmaStart = dmaIt->second.second;
                sZ64Rom.rom.clear();
                rom.reserve(rom.size());
                for (size_t i = 0; i < rom.size(); ++i) {
                    sZ64Rom.rom.push_back(rom[i]);
                }

                return true;
            }
        }
    }

    unloadOOTRom();
    return false;
}

bool isRomLoaded() {
    return sZ64Rom.rom.size() > 0;
}

RECOMP_DLL_FUNC(PMMZobj_tryLoadOOTROM) {
    RECOMP_RETURN(bool, tryLoadOOTRom());
}

RECOMP_DLL_FUNC(PMMZobj_unloadOOTROM) {
    RECOMP_RETURN(bool, unloadOOTRom());
}

RECOMP_DLL_FUNC(PMMZobj_isOOTRomLoaded) {
    RECOMP_RETURN(bool, isRomLoaded());
}

std::vector<u8> extractZ64Object(const void *romData, unsigned dmaOffset, int dmaIndex, const std::string &checksum, const fs::path &outPath) {
    const char *rom = static_cast<const char *>(romData);

    DMADataEntry entry = DMADataEntry(rom, dmaOffset, dmaIndex);

    size_t entrySize = entry.getUncompressedSize();

    if (entrySize > SIZE_OF_YAZ0_HEADER) {
        std::cout << "Extracting " << outPath.filename() << " from 0x" << std::hex << entry.physicalAddr.first << std::endl;

        bool isExtracted = false;

        uint32_t physicalStart = entry.physicalAddr.first + SIZE_OF_YAZ0_HEADER;

        std::unique_ptr<u8> buf(new u8[entrySize]);

        if (entry.isCompressed()) {
            std::cout << "Extracting compressed file..." << std::endl;

            size_t entrySizeCompressed = entry.getCompressedSize();

            if (entrySizeCompressed) {
                yaz0_decompress(entrySize, entrySizeCompressed - SIZE_OF_YAZ0_HEADER, reinterpret_cast<const uint8_t *>(rom + physicalStart), buf.get());
                isExtracted = true;
            }
        } else {
            memcpy(buf.get(), rom + physicalStart, entrySize);
            isExtracted = true;
        }

        if (isExtracted) {
            SHA1 entryChecksum;
            entryChecksum.update(std::string(reinterpret_cast<char *>(buf.get()), entrySize));

            if (checksum == "" || entryChecksum.final() == checksum) {
                if (outPath.has_parent_path()) {
                    fs::create_directories(outPath.parent_path());
                }

                std::cout << "Writing DMA entry " << std::dec << dmaIndex << " to " << outPath.string() << std::endl;
                std::ofstream fsOut(outPath, std::ios::binary);
                fsOut.write(reinterpret_cast<char *>(buf.get()), entrySize);
                fsOut.flush();
                std::cout << "Finished writing DMA entry " << std::dec << dmaIndex << " to " << outPath.string() << std::endl;

                std::vector<u8> ret;

                ret.resize(entrySize);
                for (size_t i = 0; i < entrySize; ++i) {
                    ret[i] = buf.get()[i];
                }
                return ret;
            } else {
                std::cout << "Checksum of " << outPath.filename() << " extracted from ROM did not match!" << std::endl;
            }
        }
    }

    return std::vector<u8>({});
}

bool extractOrLoadCachedOOTObject(uint8_t *rdram, recomp_context *ctx, unsigned dmaIndex, const fs::path &assetPath, const std::string &assetChecksum) {
    // clang-format off
    PTR(char) rdramBuf = RECOMP_ARG(PTR(char), 0);
    // clang-format on

    if (!rdramBuf) {
        std::cout << "Cannot load OOT object " << assetPath.filename() << " into NULL pointer!" << std::endl;
        return false;
    }

    unsigned rdramBufSize = RECOMP_ARG(unsigned, 1);

    if (assetChecksum != "") {
        fs::directory_entry assetDirEnt(assetPath);

        if (assetDirEnt.exists() && !assetDirEnt.is_directory()) {
            std::cout << "Found cached OOT object " << assetPath.filename() << "!" << std::endl;

            std::ifstream file(assetPath, std::ios::binary);

            std::string assetCandidate(std::istreambuf_iterator<char>{file}, {});

            SHA1 candidateChecksum;
            candidateChecksum.update(assetCandidate);

            if (candidateChecksum.final() == assetChecksum && writeDataToRecompBuffer(rdram, ctx, rdramBuf, rdramBufSize, assetCandidate.data(), assetCandidate.size())) {
                return true;
            } else {
                std::cout << "Cached OOT object " << assetPath.filename() << " checksum did not match! Deleting and re-extracting..." << std::endl;
                file.close();
            }
        }
    }

    if (isRomLoaded()) {

        auto obj = extractZ64Object(sZ64Rom.rom.data(), sZ64Rom.dmaStart, dmaIndex, assetChecksum, assetPath);
        if (obj.size() > 0) {
            return writeDataToRecompBuffer(rdram, ctx, rdramBuf, rdramBufSize, obj.data(), obj.size());
        };
    }

    return false;
}

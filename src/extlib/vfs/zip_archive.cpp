#include "zip_archive.hpp"

#define MINIZ_NO_STDIO
#define MINIZ_NO_DEFLATE_APIS
#include <miniz.h>

namespace Vfs {

enum {
    MZ_ZIP_LOCAL_DIR_HEADER_SIG = 0x04034b50,
    MZ_ZIP_LOCAL_DIR_HEADER_SIZE = 30,
    MZ_ZIP_LDH_FILENAME_LEN_OFS = 26,
    MZ_ZIP_LDH_EXTRA_LEN_OFS = 28,
    MZ_ZSTD = 93,
};

std::unordered_map<fs::path, std::shared_ptr<ZipArchive>> ZipArchive::cache;
std::shared_mutex ZipArchive::cacheMutex;

ZipArchive::ZipArchive(ZipArchive::Private, fs::path path)
    : path(path), stream(path, std::ios::binary | std::ios::ate) {

    stream.exceptions(std::ifstream::badbit);

    if (!stream.is_open()) {
        throw std::runtime_error("Could not open zip file");
    }

    filesize = static_cast<size_t>(stream.tellg());
    stream.seekg(0);
}

std::shared_ptr<ZipArchive> ZipArchive::checkCache(fs::path path) {
    try {
        std::shared_lock<std::shared_mutex> cacheLock(cacheMutex);
        return cache.at(path);
    } catch (const std::out_of_range& e) {
        return nullptr;
    }
}

std::shared_ptr<ZipArchive> ZipArchive::factory(fs::path path) {
    fs::path normalized = path.lexically_normal();

    if (auto cached = checkCache(normalized)) {
        return cached;
    }

    auto archive = std::shared_ptr<ZipArchive>(new ZipArchive(Private(), normalized));
    archive->init();

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        ZipArchive::cache[path] = archive;
    }

    return archive;
}

void ZipArchive::init() {
    mz_zip_archive* mz_archive = new mz_zip_archive;
    mz_zip_error mz_error;
    mz_bool mz_status;

    memset(mz_archive, 0, sizeof(mz_zip_archive));

    mz_archive->m_pIO_opaque = this;
    mz_archive->m_pRead = ZipArchive::onRead;

    mz_status = mz_zip_reader_init(mz_archive, filesize, 0);

    if (!mz_status) {
        mz_error = mz_zip_get_last_error(mz_archive);
        throw std::runtime_error("Zip archive error: " + std::string(mz_zip_get_error_string(mz_error)));
    }

    this->mz_archive = static_cast<void*>(mz_archive);
}

ZipArchive::FileInfo ZipArchive::locateFile(std::string path) {
    try {
        std::shared_lock<std::shared_mutex> fileListLock(fileListMutex);
        return fileList.at(path);
    } catch (const std::out_of_range& e) {
    }

    std::lock_guard<std::mutex> lock(mutex);

    mz_zip_archive* mz_archive = static_cast<mz_zip_archive*>(this->mz_archive);
    mz_zip_archive_file_stat stat;
    mz_zip_error mz_error;
    mz_bool mz_status;
    mz_uint index;

    index = mz_zip_reader_locate_file(mz_archive, path.c_str(), nullptr, 0);

    if (index == -1) {
        mz_error = mz_zip_get_last_error(mz_archive);
        throw std::runtime_error("Zip archive error: " + std::string(mz_zip_get_error_string(mz_error)));
    }

    mz_status = mz_zip_reader_file_stat(mz_archive, index, &stat);

    if (!mz_status) {
        mz_error = mz_zip_get_last_error(mz_archive);
        throw std::runtime_error("Zip archive error: " + std::string(mz_zip_get_error_string(mz_error)));
    }

    auto info = FileInfo{ index, stat.m_uncomp_size };

    if (stat.m_comp_size == stat.m_uncomp_size) {
        char localDirHeader[30];

        stream.seekg(stat.m_local_header_ofs, std::ios_base::beg);
        stream.read(localDirHeader, sizeof(localDirHeader));
        if (stream.fail() && !stream.eof()) {
            stream.clear();
            throw std::runtime_error("Zip archive error: Failed to read local dir header");
        }

        // Validate the local dir header.
        if (MZ_READ_LE32(localDirHeader) != MZ_ZIP_LOCAL_DIR_HEADER_SIG) {
            throw std::runtime_error("Zip archive error: Invalid local dir header");
        }

        // Skip over unused data of the header.
        uint32_t ldhFilenameLenOfs = MZ_READ_LE16(localDirHeader + MZ_ZIP_LDH_FILENAME_LEN_OFS);
        uint32_t ldhExtraLenOfs = MZ_READ_LE16(localDirHeader + MZ_ZIP_LDH_EXTRA_LEN_OFS);

        info.compressed = false;
        info.offset = stat.m_local_header_ofs + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + ldhFilenameLenOfs + ldhExtraLenOfs;
    }

    {
        std::unique_lock<std::shared_mutex> fileListLock(fileListMutex);
        fileList[path] = info;
    }

    return info;
}

void ZipArchive::extractFileToBuffer(std::string path, std::vector<uint8_t>& buffer) {
    auto info = locateFile(path);

    std::lock_guard<std::mutex> lock(mutex);

    mz_zip_archive* mz_archive = static_cast<mz_zip_archive*>(this->mz_archive);
    mz_zip_error mz_error;
    mz_bool mz_status;

    buffer.resize(info.size);

    mz_status = mz_zip_reader_extract_to_mem(mz_archive, info.index, buffer.data(), info.size, 0);

    if (!mz_status) {
        mz_error = mz_zip_get_last_error(mz_archive);
        throw std::runtime_error("ZIP error " + std::string(mz_zip_get_error_string(mz_error)));
    }
}

size_t ZipArchive::extractBytesToBuffer(void* buffer, size_t bytes, size_t offset) {
    std::lock_guard<std::mutex> lock(mutex);

    stream.seekg(offset, std::ios_base::beg);
    stream.read(reinterpret_cast<char*>(buffer), bytes);
    if (stream.fail() && !stream.eof()) {
        stream.clear();
        throw std::runtime_error("Read operation failed: " + path.string());
    }

    return static_cast<size_t>(stream.gcount());
}

size_t ZipArchive::onRead(void* datasrc, mz_uint64 offset, void* buffer, size_t bytes) {
    auto that = static_cast<ZipArchive*>(datasrc);

    that->stream.seekg(offset, std::ios_base::beg);
    that->stream.read(reinterpret_cast<char*>(buffer), bytes);
    if (that->stream.fail() && !that->stream.eof()) {
        that->stream.clear();
        throw std::runtime_error("Read operation failed: " + that->path.string());
    }

    return static_cast<size_t>(that->stream.gcount());
}

} // namespace Vfs

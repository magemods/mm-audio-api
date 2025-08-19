#include <extlib/vfs/filesystem.hpp>

#include <fstream>
#include <vector>

#include <extlib/vfs/native_file.hpp>
#include <extlib/vfs/zip_file.hpp>
#include <extlib/utils.hpp>

namespace Vfs {

void Filesystem::setDefaultDir(fs::path dir) {
    defaultDir = dir.lexically_normal();
}

void Filesystem::addAllowedDir(fs::path dir) {
    allowedDirs.insert(dir.lexically_normal());
}

bool Filesystem::isPathAllowed(fs::path path) {
    fs::path normalized = path.lexically_normal();

    for (const auto& allowed : allowedDirs) {
        if (normalized == allowed) {
            return true;
        }

        std::error_code ec;
        auto relative = fs::relative(normalized, allowed, ec);

        if (!ec && !relative.empty() && relative.string().find("..") == std::string::npos) {
            return true;
        }
    }

    return false;
}

void Filesystem::addKnownZipExtension(std::string ext) {
    knownZipExtensions.insert(lowercase(ext));
}

bool Filesystem::isZipFile(fs::path path) {
    auto ext = path.extension().string();
    if (knownZipExtensions.contains(lowercase(ext))) {
        return true;
    }

    if (knownZipFiles.contains(path)) {
        return true;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }

    std::vector<uint8_t> buffer(4);
    if (!stream.read(reinterpret_cast<char*>(buffer.data()), 4)) {
        return false;
    }

    bool isZip = (buffer[0] == 'P' && buffer[1] == 'K') &&
        ((buffer[2] == 0x03 && buffer[3] == 0x04) || (buffer[2] == 0x05 && buffer[3] == 0x06));

    if (isZip) {
        knownZipFiles.insert(path);
    }

    return isZip;
}

std::shared_ptr<File> Filesystem::openFile(std::u8string baseDirStr, std::u8string pathStr) {
    auto baseDir = fs::path(baseDirStr).lexically_normal();
    if (baseDir.is_relative() || baseDirStr.empty()) {
        baseDir = defaultDir / baseDir;
    }

    if (!isPathAllowed(baseDir)) {
        throw std::filesystem::filesystem_error("Base dir is not an allowed path", baseDir, std::error_code());
    }

    auto relativePath = fs::path(pathStr).lexically_normal();
    if (relativePath.empty() || relativePath.string().find("..") != std::string::npos) {
        throw std::filesystem::filesystem_error("Path not child of base dir", relativePath, baseDir, std::error_code());
    }

    if (isZipFile(baseDir)) {
        auto zipArchive = ZipArchive::factory(baseDir);
        if (zipArchive == nullptr) {
            throw std::filesystem::filesystem_error("Could not open ZIP file", baseDir, std::error_code());
        }
        return std::make_shared<ZipFile>(zipArchive, relativePath);
    }

    auto fullPath = baseDir / relativePath;
    return std::make_shared<NativeFile>(fullPath);
}

} // namespace Vfs

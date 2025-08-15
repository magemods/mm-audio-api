#pragma once

#include <string>
#include <filesystem>
#include <unordered_set>
#include <memory>

#include "file.hpp"

namespace fs = std::filesystem;

namespace Vfs {

class Filesystem {
public:
    void setDefaultDir(fs::path dir);
    void addAllowedDir(fs::path dir);
    bool isPathAllowed(fs::path path);
    void addKnownZipExtension(std::string ext);
    bool isZipFile(fs::path path);

    std::shared_ptr<File> openFile(std::u8string baseDirStr, std::u8string pathStr);

private:
    fs::path defaultDir;
    std::unordered_set<fs::path> allowedDirs;
    std::unordered_set<std::string> knownZipExtensions;
    std::unordered_set<fs::path> knownZipFiles;
};

} // namespace Vfs

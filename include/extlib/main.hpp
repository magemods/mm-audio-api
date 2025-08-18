#pragma once
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <extlib/resource/abstract.hpp>
#include <extlib/vfs/filesystem.hpp>

extern Vfs::Filesystem gVfs;
extern std::unordered_map<size_t, std::shared_ptr<Resource::Abstract>> gResourceData;
extern std::shared_mutex gResourceDataMutex;

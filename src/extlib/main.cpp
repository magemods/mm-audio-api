#include <extlib/main.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>

#include <audio_api/types.h>

#include <extlib/lib_recomp.hpp>
#include <extlib/resource/abstract.hpp>
#include <extlib/resource/audiofile.hpp>
#include <extlib/resource/generic.hpp>
#include <extlib/thread.hpp>

extern "C" {
    DLLEXPORT uint32_t recomp_api_version = RECOMP_API_VERSION;
}

namespace fs = std::filesystem;

static bool sIsInitialized = false;
static size_t sResourceCount = 0;

Vfs::Filesystem gVfs;
std::unordered_map<size_t, std::shared_ptr<Resource::Abstract>> gResourceData;
std::shared_mutex gResourceDataMutex;

static plog::ConsoleAppender<plog::TxtFormatter> sConsoleAppender;

RECOMP_DLL_FUNC(AudioApiNative_Init) {
    auto logLevel = RECOMP_ARG(uint32_t, 0);
    auto rootDirStr = RECOMP_ARG_U8STR(1);

    try {
        if (sIsInitialized) {
            throw std::runtime_error("Extlib already initialized");
        }

        plog::init((plog::Severity)logLevel, &sConsoleAppender);

        {
            auto rootDir = fs::canonical(fs::path(rootDirStr).parent_path());
            auto defaultDir = rootDir / "mod_data" / "audio";
            PLOG_INFO << "Root Dir: " << rootDir;
            PLOG_INFO << "Default Dir: " << defaultDir;

            gVfs.setDefaultDir(defaultDir);
            gVfs.addAllowedDir(rootDir / "mod_data");
            gVfs.addAllowedDir(rootDir / "mods");

            gVfs.addKnownZipExtension(".zip");
            gVfs.addKnownZipExtension(".nrm");
            gVfs.addKnownZipExtension(".mmrs");
        }

        {
            std::thread workerThread(workerThreadLoop);
            workerThread.detach();
        }

        sIsInitialized = true;
        RECOMP_RETURN(bool, true);

    } catch (const fs::filesystem_error& e) {
        PLOG_ERROR << "Init error: " << e.what();
    } catch (const std::invalid_argument& e) {
        PLOG_ERROR << "Init error: " << e.what();
    } catch (const std::runtime_error& e) {
        PLOG_ERROR << "Init error: " << e.what();
    } catch (...) {
        PLOG_ERROR << "Init error: Unknown error";
    }

    RECOMP_RETURN(bool, false);
}

RECOMP_DLL_FUNC(AudioApiNative_Ready) {
    RECOMP_RETURN(bool, true);
}

RECOMP_DLL_FUNC(AudioApiNative_Tick) {
    workerThreadNotify();
    RECOMP_RETURN(bool, true);
}

RECOMP_DLL_FUNC(AudioApiNative_Dma) {
    auto ptr = RECOMP_ARG(int32_t, 0);
    size_t size = RECOMP_ARG(uint32_t, 1);
    size_t offset = RECOMP_ARG(uint32_t, 2);

    auto args = TO_PTR(uint32_t, RECOMP_ARG(int32_t, 3));
    size_t resourceId = args[0];

    try {
        std::shared_ptr<Resource::Abstract> resource;
        {
            std::shared_lock<std::shared_mutex> lock(gResourceDataMutex);

            auto it = gResourceData.find(resourceId);
            if (it == gResourceData.end()) {
                throw std::invalid_argument("Invalid resourceId " + std::to_string(resourceId));
            }

            resource = std::static_pointer_cast<Resource::Abstract>(it->second);
        }

        resource->dma(rdram, ptr, offset, size, args[1], args[2]);
        queuePreload(resourceId);

        RECOMP_RETURN(bool, true);

    } catch (const fs::filesystem_error& e) {
        PLOG_ERROR << "DMA Error: " << e.what();
    } catch (const std::invalid_argument& e) {
        PLOG_ERROR << "DMA Error: " << e.what();
    } catch (const std::runtime_error& e) {
        PLOG_ERROR << "DMA Error: " << e.what();
    } catch (...) {
        PLOG_ERROR << "DMA Error: Unknown error";
    }

    RECOMP_RETURN(bool, false);
}

RECOMP_DLL_FUNC(AudioApiNative_AddResource) {
    auto info = RECOMP_ARG(AudioApiResourceInfo*, 0);
    auto baseDir = RECOMP_ARG_U8STR(1);
    auto path = RECOMP_ARG_U8STR(2);
    auto cacheStrategy = Resource::parseCacheStrategy(info->cacheStrategy);

    try {
        // TODO: if info->filesize exists, avoid opening file and just check that it exists
        auto file = gVfs.openFile(baseDir, path);
        auto resource = std::make_shared<Resource::Generic>(file, cacheStrategy);

        info->resourceId = sResourceCount++;
        info->filesize = resource->size();
        file->close();

        {
            std::unique_lock<std::shared_mutex> lock(gResourceDataMutex);
            gResourceData[info->resourceId] = std::move(resource);
        }

        queuePreload(info->resourceId);

        RECOMP_RETURN(bool, true);

    } catch (const fs::filesystem_error& e) {
        PLOG_ERROR << "Error adding resource: " << e.what();
    } catch (const std::invalid_argument& e) {
        PLOG_ERROR << "Error adding resource: " << e.what();
    } catch (const std::runtime_error& e) {
        PLOG_ERROR << "Error adding resource: " << e.what();
    } catch (...) {
        PLOG_ERROR << "Error adding resource: Unknown error";
    }

    RECOMP_RETURN(bool, false);
}

RECOMP_DLL_FUNC(AudioApiNative_AddAudioFile) {
    auto info = RECOMP_ARG(AudioApiFileInfo*, 0);
    auto baseDir = RECOMP_ARG_U8STR(1);
    auto path = RECOMP_ARG_U8STR(2);
    auto codec = Decoder::parseType(info->codec);
    auto cacheStrategy = Resource::parseCacheStrategy(info->cacheStrategy);

    try {
        auto file = gVfs.openFile(baseDir, path);
        auto resource = std::make_shared<Resource::Audiofile>(file, codec, cacheStrategy);

        if (info->trackCount && info->sampleCount) {
            resource->metadata->setTrackCount(info->trackCount);
            resource->metadata->setSampleRate(info->sampleRate);
            resource->metadata->setSampleCount(info->sampleCount);
            resource->metadata->setLoopInfo(info->loopStart, info->loopEnd, info->loopCount);
            file->close();
        } else {
            resource->open();
            resource->probe();
            resource->close();
        }

        info->resourceId  = sResourceCount++;
        info->trackCount  = resource->metadata->trackCount;
        info->sampleRate  = resource->metadata->sampleRate;
        info->sampleCount = resource->metadata->sampleCount;
        info->loopStart   = resource->metadata->loopStart;
        info->loopEnd     = resource->metadata->loopEnd;
        info->loopCount   = resource->metadata->loopCount;

        {
            std::unique_lock<std::shared_mutex> lock(gResourceDataMutex);
            gResourceData[info->resourceId] = std::move(resource);
        }

        queuePreload(info->resourceId);

        RECOMP_RETURN(bool, true);

    } catch (const fs::filesystem_error& e) {
        PLOG_ERROR << "Error probing file: " << e.what();
    } catch (const std::invalid_argument& e) {
        PLOG_ERROR << "Error probing file: " << e.what();
    } catch (const std::runtime_error& e) {
        PLOG_ERROR << "Error probing file: " << e.what();
        //PLOG_ERROR << "Error probing file: " << baseDir << "/" << path << " Reason: " << e.what();
    } catch (...) {
        PLOG_ERROR << "Error probing file: Unknown error";
    }

    RECOMP_RETURN(bool, false);
}

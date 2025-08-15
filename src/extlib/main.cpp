#include "main.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>

#include "lib_recomp.hpp"
#include "native_bridge.h"
#include "resource/abstract.hpp"
#include "resource/sample.hpp"
#include "thread.hpp"

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
    uint32_t logLevel = RECOMP_ARG(uint32_t, 0);
    std::u8string rootDirStr = RECOMP_ARG_U8STR(1);

    try {
        if (sIsInitialized) {
            throw std::runtime_error("Extlib already initialized");
        }

        plog::init((plog::Severity)logLevel, &sConsoleAppender);

        {
            auto rootDir = fs::canonical(fs::path(rootDirStr).parent_path().parent_path());
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
    int32_t ptr = RECOMP_ARG(int32_t, 0);
    size_t count = RECOMP_ARG(uint32_t, 1);
    size_t offset = RECOMP_ARG(uint32_t, 2);

    DmaRequestArgs* args = TO_PTR(DmaRequestArgs, RECOMP_ARG(int32_t, 3));
    size_t resourceId = args->arg0;

    try {
        std::shared_ptr<Resource::Sample> resource;
        {
            std::shared_lock<std::shared_mutex> lock(gResourceDataMutex);

            auto it = gResourceData.find(resourceId);
            if (it == gResourceData.end()) {
                throw std::invalid_argument("Invalid resourceId " + std::to_string(resourceId));
            }

            resource = std::static_pointer_cast<Resource::Sample>(it->second);
        }

        resource->dma(rdram, ptr, offset, count, args->arg1, args->arg2);
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


RECOMP_DLL_FUNC(AudioApiNative_AddAudioFile) {
    AudioFileInfo* info = RECOMP_ARG(AudioFileInfo*, 0);
    std::u8string baseDir = RECOMP_ARG_U8STR(1);
    std::u8string path = RECOMP_ARG_U8STR(2);

    try {
        auto file = gVfs.openFile(baseDir, path);
        auto resource = std::make_shared<Resource::Sample>(file);

        if (info->trackCount && info->sampleCount) {

        }

        resource->open();
        resource->probe();
        resource->close();

        info->resourceId  = sResourceCount++;

        // todo, check if set already, if so not probe
        info->trackCount  = resource->metadata->trackCount;
        info->sampleRate  = resource->metadata->sampleRate;
        info->sampleCount = resource->metadata->sampleCount;
        info->loopStart   = resource->metadata->loopStart;
        info->loopEnd     = resource->metadata->loopEnd;
        info->loopCount   = resource->metadata->loopCount;

        PLOG_DEBUG << "Sample count: " << info->sampleCount;
        PLOG_DEBUG << "Loop start: " << info->loopStart;
        PLOG_DEBUG << "Loop end: " << info->loopEnd;

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

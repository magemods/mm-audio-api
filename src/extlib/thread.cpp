#include <extlib/thread.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include <plog/Log.h>

#include <extlib/main.hpp>
#include <extlib/resource/abstract.hpp>
#include <extlib/utils.hpp>

constexpr int GC_INTERVAL_SECONDS = 1;

std::thread::id gMainThreadId = std::this_thread::get_id();
std::thread::id gWorkerThreadId;

static std::condition_variable sWorkerThreadSignal;
static std::mutex sWorkerThreadMutex;

static std::unordered_set<size_t> sPreloadRequests;
static std::mutex sPreloadMutex;

static std::chrono::steady_clock::time_point sLastGc = EPOCH;


void drainPreload();
void gc();

void workerThreadNotify() {
    sWorkerThreadSignal.notify_one();
}

void workerThreadLoop() {
    gWorkerThreadId = std::this_thread::get_id();

    while (true) {
        {
            std::unique_lock<std::mutex> lock(sWorkerThreadMutex);
            sWorkerThreadSignal.wait_for(lock, std::chrono::seconds(GC_INTERVAL_SECONDS));
        }

        drainPreload();

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - sLastGc);

        if (elapsed.count() > GC_INTERVAL_SECONDS) {
            gc();
            sLastGc = std::chrono::steady_clock::now();
        }
    }
}

void queuePreload(size_t resourceId) {
    std::unique_lock<std::mutex> preloadLock(sPreloadMutex);
    sPreloadRequests.insert(resourceId);
}

void drainPreload() {
    std::unordered_set<size_t> preloadRequests;
    std::vector<std::pair<Resource::ResourcePtr, Resource::PreloadTask>> tasks;

    {
        std::unique_lock<std::mutex> preloadLock(sPreloadMutex);
        preloadRequests.merge(sPreloadRequests);
    }

    {
        std::shared_lock<std::shared_mutex> resourceLock(gResourceDataMutex);

        for (const auto& resourceId : preloadRequests) {
            auto it = gResourceData.find(resourceId);
            if (it == gResourceData.end()) {
                continue;
            }

            auto resource = it->second;
            for (const auto& task : resource->getPreloadTasks()) {
                tasks.emplace_back(resource, task);
            }
        }
    }

    std::sort(tasks.begin(), tasks.end(), [](const auto& a, const auto& b) {
        return a.second.priority < b.second.priority;
    });

    for (const auto& [ resource, task ] : tasks) {
        try {
            resource->runPreloadTask(task);
        } catch (const std::runtime_error& e) {
            PLOG_ERROR << "Error running preload task: " << e.what();
        } catch (...) {
            PLOG_ERROR << "Error running preload task: Unknown error";
        }
    }
}

void gc() {
    std::shared_lock<std::shared_mutex> lock(gResourceDataMutex);

    for (const auto& [ resourceId, resource ] : gResourceData) {
        resource->gc();
    }
}

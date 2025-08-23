#pragma once
#include <cstddef>
#include <thread>

extern std::thread::id gMainThreadId;
extern std::thread::id gWorkerThreadId;

void workerThreadNotify();
void workerThreadLoop();
void queuePreload(size_t resourceId);

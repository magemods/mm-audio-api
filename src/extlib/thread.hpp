#pragma once
#include <cstddef>

void workerThreadNotify();
void workerThreadLoop();
void queuePreload(size_t resourceId);

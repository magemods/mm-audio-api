#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <queue>

#include "lib_recomp.hpp"
#include "audioapi.h"

namespace fs = std::filesystem;

class DmaController {
public:
    DmaController(uint8_t* p_recomp_rdram, fs::path p_path);
    ~DmaController();

    uint8_t* getRdram();
    void setRdram(uint8_t* p_recomp_rdram);

private:
    uint8_t* recomp_rdram = NULL;
    fs::path audio_path;
};

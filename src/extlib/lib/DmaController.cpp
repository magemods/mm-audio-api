#include <iostream>
#include <string.h>
#include "plog/Log.h"

#include "DmaController.hpp"

DmaController::DmaController(uint8_t* p_recomp_rdram, fs::path p_path) {
    recomp_rdram = p_recomp_rdram;
    audio_path = fs::path(p_path) / ".." / "audio";
}

DmaController::~DmaController() {}

uint8_t* DmaController::getRdram() {
    return recomp_rdram;
}

void DmaController::setRdram(uint8_t* p_recomp_rdram) {
    recomp_rdram = p_recomp_rdram;
}

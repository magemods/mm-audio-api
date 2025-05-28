#include <filesystem>
#include <iostream>
#include <string>
#include <bit>
#include <map>
#include <string.h>

#include "plog/Log.h"
#include "plog/Init.h"
#include "plog/Formatters/TxtFormatter.h"
#include "plog/Initializers/ConsoleInitializer.h"

#include "audioapi.h"
#include "lib_recomp.hpp"

#include "./lib/DmaController.hpp"
#include "audio/DetectiveSky612-DanceOfDeath.xxd"

extern "C" {
    DLLEXPORT uint32_t recomp_api_version = 1;
}

std::shared_ptr<DmaController> controller = NULL;

RECOMP_DLL_FUNC(AudioApiNative_Init) {
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init((plog::Severity)RECOMP_ARG(unsigned int, 0), &consoleAppender);

    fs::path savepath = fs::path(RECOMP_ARG_U8STR(1));
    controller = std::make_shared<DmaController>(rdram, savepath);
}

/*
RECOMP_DLL_FUNC(AudioApi_GetSequence) {
    RECOMP_RETURN(PTR(char), __03_zseq);
}
*/

RECOMP_DLL_FUNC(AudioApiNative_GetSequenceSize) {
    RECOMP_RETURN(s32, sizeof(__03_zseq));
}

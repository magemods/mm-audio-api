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

#include "audio/DetectiveSky612-DanceOfDeath.xxd"

extern "C" {
    DLLEXPORT uint32_t recomp_api_version = 1;
}

/*
RECOMP_DLL_FUNC(AudioApi_GetSequence) {
    RECOMP_RETURN(PTR(char), __03_zseq);
}
*/

RECOMP_DLL_FUNC(AudioApi_GetSequenceSize) {
    RECOMP_RETURN(s32, sizeof(__03_zseq));
}

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

#include "lib_recomp.hpp"

extern "C" {
    DLLEXPORT uint32_t recomp_api_version = 1;

}

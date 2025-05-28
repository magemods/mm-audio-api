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
#include "achievements.h"

#include "sqlite3.h"
#include "lib_recomp.hpp"

#include "./ach_data/AchievementController.hpp"

 
extern "C" {
    DLLEXPORT uint32_t recomp_api_version = 1;
    
}

std::shared_ptr<AchievementController> controller = NULL;



RECOMP_DLL_FUNC(AchievementNative_Init) {
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init((plog::Severity)RECOMP_ARG(unsigned int, 0), &consoleAppender);

    unsigned int number_of_slots = RECOMP_ARG(unsigned int, 1);
    fs::path savepath = fs::path(RECOMP_ARG_U8STR(2));
    controller = std::make_shared<AchievementController>(rdram, number_of_slots, savepath);
}

RECOMP_DLL_FUNC(AchievementNative_Declare) {
    std::string ach_set = RECOMP_ARG_STR(0);
    PTR(Achievement) achievement = RECOMP_ARG(PTR(Achievement), 1);
    controller->setRdram(rdram);
    controller->declareAchievement(ach_set, achievement);
}

RECOMP_DLL_FUNC(AchievementNative_SetU32Flag) {
    std::string achievement_set = RECOMP_ARG_STR(0);
    std::string achievement_id = RECOMP_ARG_STR(1);
    u32 slot = RECOMP_ARG(u32, 2);
    u32 value = RECOMP_ARG(u32, 3);

    controller->setU32Flag(achievement_set, achievement_id, slot, value);

    // PLOGI.printf("Set Achievement '%s' to %i\n", achievement_id.c_str(), value);
}

RECOMP_DLL_FUNC(AchievementNative_GetNextAchievementUnlock) {


    RECOMP_RETURN(PTR(Achievement), controller->getNextAchievementUnlock());

    // PLOGI.printf("Set Achievement '%s' to %i\n", achievement_id.c_str(), value);
}

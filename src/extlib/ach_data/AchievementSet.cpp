#include <string.h>
#include "plog/Log.h"

#include "AchievementSet.hpp"

#include "AchievementFlag.hpp"
#include "AchievementWrapper.hpp"
#include "AchievementController.hpp"

AchievementSet::AchievementSet(AchievementController* p_controller, std::string p_ach_set) {
    controller = p_controller;
    ach_set = p_ach_set;
}

AchievementSet::~AchievementSet(){}

std::shared_ptr<AchievementFlag> AchievementSet::getFlag(std::string flag_id) {
    return flags.at(flag_id);
}

void AchievementSet::declareAchievement(PTR(Achievement) achievement) {

    // Create the achievement wrapper:
    std::shared_ptr<AchievementWrapper> new_ach = std::make_shared<AchievementWrapper>(controller, ach_set, achievement);
    auto ach_pair = std::pair<std::string, std::shared_ptr<AchievementWrapper>>(new_ach->getId(), new_ach);

    // Create the default flag for the achievement:
    std::shared_ptr<AchievementFlag> new_flag = std::make_shared<AchievementFlag>(controller, ach_set, new_ach->getNativePtr());
    auto flag_pair = std::pair<std::string, std::shared_ptr<AchievementFlag>>(new_flag->getId(), new_flag);

    PLOGD.printf("Adding achievement '%s' to set '%s'...\n", new_ach->getId().c_str(), ach_set.c_str());
    flags.insert(flag_pair);
    achievments.insert(ach_pair);

    // Link 'em together:
    new_flag->addDependentAchievement(new_ach);
    new_ach->addRequiredFlag(new_flag); // TODO: handle additional flags... once those are implemented.
}
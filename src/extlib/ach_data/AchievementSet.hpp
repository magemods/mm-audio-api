#pragma once

#include <string>
#include <map>
#include <memory>
#include <unordered_map>
#include "achievements.h"
#include "lib_recomp.hpp"

class AchievementController;
class AchievementFlag;
class AchievementWrapper;

class AchievementSet {
public:
    
    AchievementSet(AchievementController* p_controller, std::string p_ach_set);
    ~AchievementSet();

    void declareAchievement(PTR(Achievement) achievement);
    std::shared_ptr<AchievementFlag> getFlag(std::string flag_id);

private:
    
    AchievementController* controller = NULL;
    std::string ach_set;

    std::unordered_map<std::string, std::shared_ptr<AchievementWrapper>> achievments;
    std::unordered_map<std::string, std::shared_ptr<AchievementFlag>> flags;

};
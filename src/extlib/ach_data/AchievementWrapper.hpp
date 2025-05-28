#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include "lib_recomp.hpp"
#include "achievements.h"

class AchievementController;
class AchievementFlag;

class AchievementWrapper {
public:
    AchievementWrapper(AchievementController* p_controller, std::string p_ach_set, PTR(Achievement) p_achievement);
    ~AchievementWrapper();

    std::string getId();
    std::string getDisplayName();
    std::string getDescription();

    Achievement* getNativePtr();
    void addRequiredFlag(std::shared_ptr<AchievementFlag> flag);
    void updateUnlock(unsigned int slot);
    bool standardIsUnlocked(unsigned int slot);

private:
    AchievementController* controller = NULL;
    std::string ach_set;
    PTR(Achievement) recomp_address = 0;
    bool is_unlocked = false;
    std::unordered_map<std::string, std::shared_ptr<AchievementFlag>> required_flags;


};
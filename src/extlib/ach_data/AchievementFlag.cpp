#include "plog/Log.h"

#include "AchievementFlag.hpp"
#include "AchievementController.hpp"
#include "AchievementWrapper.hpp"

AchievementFlag::AchievementFlag(AchievementController* p_controller, std::string p_ach_set, Achievement* p_achievement) {
    controller = p_controller;
    ach_set = p_ach_set;
    achievement = p_achievement;
    
    data_size = ACHIEVEMENT_FLAG_SIZE;
    data = new unsigned char*[controller->getNumberOfSlots()];
    for (unsigned i = 0; i < controller->getNumberOfSlots(); i++) {
        data[i] = new unsigned char[ACHIEVEMENT_FLAG_SIZE];
        loadDefaultValue(i);
    }
}


AchievementFlag::~AchievementFlag() {
    for (unsigned i = 0; controller->getNumberOfSlots(); i++) {
        delete data[i];
    }
    delete data;
}

std::string AchievementFlag::getId() {
    uint8_t* rdram = controller->getRdram();
    if (achievement != NULL) {
        return ptr_to_string(controller->getRdram(), (int32_t)achievement->id);
    }

    else if (flag != NULL) {
        return ptr_to_string(controller->getRdram(), (int32_t)flag->id);
    }
    throw std::runtime_error("AchievementFlag::achievement and AchievementFlag::flag cannot both be null!");
}

AchievementFlagType AchievementFlag::getType() {
    if (flag == NULL) {
        return ACHIEVEMENT_FLAG_UNSIGNED_INT;
    }
    return flag->type;
}

void AchievementFlag::addDependentAchievement(std::shared_ptr<AchievementWrapper> ach) {
    auto pair = std::pair<std::string, std::shared_ptr<AchievementWrapper>>(ach->getId(), ach);

    dependent_achievements.insert(pair);
    // printf("Achievement %s now depends on flag %s\n", ach->getId().c_str(), this->getId().c_str());
}

void AchievementFlag::loadFromDb(unsigned int slot) {
    if (controller->dbHasFlag(ach_set, getId(), slot)) {
        controller->dbGetFlag(ach_set, getId(), slot, data_size, &data[slot]);
    }
    // We've already set the loaded slot to it's default value. No special processing is needed for
    // flags not in the database yet.
}

void AchievementFlag::saveToDb(unsigned int slot) {
    controller->dbSetFlag(ach_set, getId(), slot, data_size, &data[slot]);
}

void AchievementFlag::loadDefaultValue(unsigned int slot) {
    (*data[slot]) = 0;
}

void AchievementFlag::setValue(unsigned int slot, void* addr) {
    memcpy(&data[slot], addr, data_size);
    // PLOGI.printf("Slot now equals = %i\n", (int)data[slot][3]);
    saveToDb(slot);
}

void AchievementFlag::getValue(unsigned int slot, void* addr) {
    memcpy(addr, &data[slot], data_size);
}

void AchievementFlag::updateAchievements(unsigned int slot) {
    // Checking all dependent achievements:
    for (auto iter: dependent_achievements){
        iter.second->updateUnlock(slot);
    }
}
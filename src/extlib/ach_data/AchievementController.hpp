#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <queue>

class AchievementSet;

#include "sqlite3.h"
#include "lib_recomp.hpp"
#include "achievements.h"

#define DB_FILE_EXT ".AchievementNative.db"
#define DB_FLAG_TABLE "AchievementFlag"
#define DB_UNLOCK_TABLE "AchievementUnlock"

namespace fs = std::filesystem;

class AchievementController {
public: 
    AchievementController(uint8_t* p_recomp_rdram, unsigned int p_number_of_slots, fs::path p_path);
    ~AchievementController();

    uint8_t* getRdram();
    void setRdram(uint8_t* p_recomp_rdram);
    unsigned int getNumberOfSlots();

    void setU32Flag(std::string ach_set, std::string flag_id, unsigned int slot, unsigned int value);
    void setS32Flag(std::string ach_set, std::string flag_id, unsigned int slot, int value);
    void setF32Flag(std::string ach_set, std::string flag_id, unsigned int slot, float value);

    void declareAchievement(std::string ach_set, PTR(Achievement) achievement);
    void enqueueAchievementUnlock(PTR(Achievement) achievement);
    PTR(Achievement) getNextAchievementUnlock();
    
    // Database Handling:
    int initDatabase(fs::path p_path);
    int updateSavePath(fs::path p_path);

    int dbSetAchievement(std::string ach_set, std::string flag_id, int unlocked);
    // int dbGetAchievement(std::string ach_set, std::string flag_id);
    // int dbHasAchievement(std::string ach_set, std::string flag_id);

    int dbSetFlag(std::string ach_set, std::string flag_id, unsigned int slot, size_t size, void* data);
    int dbGetFlag(std::string ach_set, std::string flag_id, unsigned int slot, size_t size, void* write_data);
    int dbHasFlag(std::string ach_set, std::string flag_id, unsigned int slot);
    int dbDeleteFlag(std::string ach_set, std::string flag_id, unsigned int slot);
    int dbDeleteSlotFlags(unsigned int slot);
    int dbCopySlotFlags(unsigned int dst_slot, unsigned int src_slot);

    int dbSaveSOTValues(unsigned int slot);
    int dbRevertToSOTValues(unsigned int slot);

private:
    uint8_t* recomp_rdram = NULL;
    unsigned int number_of_slots = 0;
    sqlite3* db;
    int kvState = -1;
    fs::path db_path;

    std::unordered_map<std::string, std::shared_ptr<AchievementSet>> achievement_sets;
    std::queue<PTR(Achievement)> unlocked_queue;
};
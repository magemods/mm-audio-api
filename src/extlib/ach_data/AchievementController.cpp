#include <iostream>
#include <string.h>
#include "plog/Log.h"

#include "AchievementController.hpp"

#include "AchievementWrapper.hpp"
#include "AchievementFlag.hpp"
#include "AchievementSet.hpp"

AchievementController::AchievementController(uint8_t* p_recomp_rdram, unsigned int p_number_of_slots, fs::path p_path) {
    recomp_rdram = p_recomp_rdram;
    number_of_slots = p_number_of_slots;
    updateSavePath(p_path);
}

AchievementController::~AchievementController() {}

uint8_t* AchievementController::getRdram() {
    return recomp_rdram;
}

void AchievementController::setRdram(uint8_t* p_recomp_rdram) {
    recomp_rdram = p_recomp_rdram;
}

unsigned int AchievementController::getNumberOfSlots() {
    return number_of_slots;
}


void AchievementController::setU32Flag(std::string ach_set, std::string flag_id, unsigned int slot, unsigned int value){
    std::shared_ptr<AchievementSet> set = achievement_sets.at(ach_set);
    std::shared_ptr<AchievementFlag> flag = set->getFlag(flag_id);

    flag->getValue(slot, &value);
    flag->updateAchievements(slot);
}

void AchievementController::setS32Flag(std::string ach_set, std::string flag_id, unsigned int slot, int value) {

}

void AchievementController::setF32Flag(std::string ach_set, std::string flag_id, unsigned int slot, float value) {

}

// Achievement Setup:
void AchievementController::declareAchievement(std::string ach_set, PTR(Achievement) achievement) {
    if (!achievement_sets.contains(ach_set)) {
        PLOGD.printf("Creating new achievement set %s\n", ach_set.c_str());
        std::shared_ptr<AchievementSet> new_set = std::make_shared<AchievementSet>(this, ach_set);

        auto pair = std::pair<std::string, std::shared_ptr<AchievementSet>>(ach_set, new_set);
        achievement_sets.insert(pair);
    }
    achievement_sets.at(ach_set)->declareAchievement(achievement);

}

void AchievementController::enqueueAchievementUnlock(PTR(Achievement) recomp_achievement_ptr) {
    unlocked_queue.push(recomp_achievement_ptr);
}

PTR(Achievement) AchievementController::getNextAchievementUnlock() {
    if (!unlocked_queue.size()) {
        return 0;
    }
    int32_t retVal = unlocked_queue.front();
    unlocked_queue.pop();
    return retVal;
}

// Database Stuff:
int AchievementController::initDatabase(fs::path p_path) {
    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        printf("[AchievementNative] Failed init, can't open database: %s\n", sqlite3_errmsg(db));
        kvState = 0;
        return kvState;
    }

    const char *flag_sql = 
        "CREATE TABLE IF NOT EXISTS " DB_FLAG_TABLE " ("
        "ach_set TEXT,"
        "flag_id TEXT,"
        "slot INTEGER,"
        "value BLOB NOT NULL,"
        "sot_value BLOB NOT NULL,"
        "PRIMARY KEY(ach_set, flag_id, slot)"
    ");";
    
    kvState = sqlite3_exec(db, flag_sql, 0, 0, 0) == SQLITE_OK;
    if (!kvState) {
        PLOGE.printf("[AchievementFlag] Failed init, failed table '%s' creation: %s\n", DB_FLAG_TABLE, sqlite3_errmsg(db));
    } else {
        PLOGI.printf("[AchievementFlag] Initialized '%s'\n", DB_FLAG_TABLE);
    }

    const char *unlock_sql = 
        "CREATE TABLE IF NOT EXISTS " DB_UNLOCK_TABLE " ("
        "ach_set TEXT,"
        "achievement_id TEXT,"
        "unlocked INTEGER,"
        "PRIMARY KEY(ach_set, achievement_id)"
    ");";
    
    kvState = sqlite3_exec(db, unlock_sql, 0, 0, 0) == SQLITE_OK;
    if (!kvState) {
        PLOGE.printf("[AchievementNative] Failed init, failed table '%s' creation: %s\n", DB_UNLOCK_TABLE, sqlite3_errmsg(db));
    } else {
        PLOGI.printf("[AchievementNative] Initialized '%s'\n", DB_UNLOCK_TABLE);
    }

    return kvState;

}

int AchievementController::updateSavePath(fs::path p_path) {
    fs::path new_path = fs::path(p_path).replace_extension(DB_FILE_EXT);

    if (kvState == -1) {
        db_path = new_path;
        PLOGI.printf("[AchievementNative] Initializing database file at %s\n", new_path.string().c_str());
        initDatabase(new_path);
        return kvState;
    } else if (new_path != db_path) {
        // Restarting DB
        PLOGI.printf("[AchievementNative] Reloading database file at %s\n", new_path.string().c_str());
        db_path = new_path;
        sqlite3_close(db);
        initDatabase(new_path);
        return kvState;
    } else {
        // printf("[ProxyRecomp_KV] No state change needed. %s\n", new_path.string().c_str());
    }
    return kvState;
}


int AchievementController::dbSetAchievement(std::string ach_set, std::string flag_id, int unlocked) {
    if (!kvState) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed SET %s (ach_set %s): %s\n", ach_set.c_str(), flag_id.c_str(), sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "INSERT INTO " DB_UNLOCK_TABLE " (ach_set, achievement_id, unlocked) VALUES (?, ?, ?) ON CONFLICT(ach_set, achievement_id) DO UPDATE SET unlocked = excluded.unlocked;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed SET %s (ach_set %s): %s\n", ach_set.c_str(), flag_id.c_str(), sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, unlocked);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed SET %s (ach_set %s): %s\n", ach_set.c_str(), flag_id.c_str(), sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    return res;
}
/*
int AchievementController::dbGetAchievement(std::string ach_set, std::string flag_id, unsigned int slot, size_t size, void* write_data) {
    if (!kvState) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT value FROM " DB_FLAG_TABLE " WHERE ach_set = ? AND flag_id = ? AND slot = ?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, slot);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        size_t stored_size = sqlite3_column_bytes(stmt, 0);
        if (stored_size != size) {  // Fail if sizes don't match
            PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return 0;
        }
        memcpy(write_data, sqlite3_column_blob(stmt, 0), stored_size);
        sqlite3_finalize(stmt);
        return 1;
    }

    // No need to log this I don't think?
    sqlite3_finalize(stmt);
    return 0;
}

int AchievementController::dbHasAchievement(std::string ach_set, std::string flag_id, unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT 1 FROM " DB_FLAG_TABLE " WHERE flag_id = ? AND slot = ? LIMIT 1;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_UNLOCK_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, slot);

    // Return 1 if the key exists, 0 otherwise
    int exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}
*/

int AchievementController::dbSetFlag(std::string ach_set, std::string flag_id, unsigned int slot, size_t size, void* data) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "INSERT INTO " DB_FLAG_TABLE " (ach_set, flag_id, slot, value) VALUES (?, ?, ?, ?) ON CONFLICT(ach_set, flag_id, slot) DO UPDATE SET value = excluded.value;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, slot);
    sqlite3_bind_blob(stmt, 4, data, size, SQLITE_STATIC);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    return res;
}

int AchievementController::dbGetFlag(std::string ach_set, std::string flag_id, unsigned int slot, size_t size, void* write_data) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT value FROM " DB_FLAG_TABLE " WHERE ach_set = ? AND flag_id = ? AND slot = ?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, slot);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        size_t stored_size = sqlite3_column_bytes(stmt, 0);
        if (stored_size != size) {  // Fail if sizes don't match
            PLOGE.printf("[" DB_FLAG_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return 0;
        }
        memcpy(write_data, sqlite3_column_blob(stmt, 0), stored_size);
        sqlite3_finalize(stmt);
        return 1;
    }

    // No need to log this I don't think?
    sqlite3_finalize(stmt);
    return 0;
}

int AchievementController::dbHasFlag(std::string ach_set, std::string flag_id, unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT 1 FROM " DB_FLAG_TABLE " WHERE flag_id = ? AND slot = ? LIMIT 1;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, slot);

    // Return 1 if the key exists, 0 otherwise
    int exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

int AchievementController::dbDeleteFlag(std::string ach_set, std::string flag_id, unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "DELETE FROM " DB_FLAG_TABLE " REMOVE ach_set = ? AND flag_id = ? AND slot = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed GET %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ach_set.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, flag_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, slot);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE %s (ach_set %s, slot %d): %s\n", ach_set.c_str(), flag_id.c_str(), slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return res;
}

int AchievementController::dbDeleteSlotFlags(unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "DELETE FROM " DB_FLAG_TABLE " WHERE slot = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_int(stmt, 1, slot);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed REMOVE slot %d: %s\n", slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    PLOGE.printf("[" DB_FLAG_TABLE "] REMOVE slot %d succeeded\n", slot);
    return res;
}

int AchievementController::dbCopySlotFlags(unsigned int new_slot, unsigned int old_slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed COPY slot %d -> slot %d: %s\n", old_slot, new_slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "Insert INTO " DB_FLAG_TABLE " (ach_set, flag_id, slot, value) SELECT ach_set, flag_id, ?, value FROM "
        DB_FLAG_TABLE " WHERE slot = ? ON CONFLICT(ach_set, flag_id, slot) DO UPDATE SET value = excluded.value;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed COPY slot %d -> slot %d: %s\n", old_slot, new_slot, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_int(stmt, 1, new_slot);
    sqlite3_bind_int(stmt, 2, old_slot);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed COPY slot %d -> slot %d: %s\n", old_slot, new_slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return res;
}

int AchievementController::dbSaveSOTValues(unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_SAVE for slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "UPDATE " DB_FLAG_TABLE " SET sot_value = value WHERE slot = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_SAVE for slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, slot);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_SAVE for slot %d: %s\n", slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    PLOGE.printf("[" DB_FLAG_TABLE "] SOT_SAVE for slot %d succeeded\n", slot);
    return res;
}

int AchievementController::dbRevertToSOTValues(unsigned int slot) {
    if (!kvState) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_REVERT for slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "UPDATE " DB_FLAG_TABLE " SET sot_value = value WHERE slot = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_REVERT for slot %d: %s\n", slot, sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, slot);
    int res = sqlite3_step(stmt) == SQLITE_DONE;
    if (!res) {
        PLOGE.printf("[" DB_FLAG_TABLE "] Failed SOT_REVERT for slot %d: %s\n", slot, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    PLOGE.printf("[" DB_FLAG_TABLE "] SOT_REVERT for slot %d succeeded\n", slot);
    return res;
}


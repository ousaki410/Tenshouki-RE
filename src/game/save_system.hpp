/**
 * save_system.hpp - Save/load system for Phase 4
 *
 * Provides JSON-based save/load functionality for game progress:
 *   - Game date and clock state
 * *   - Castle data (facilities, garrison, resources, ownership)
 *   - Officer assignments and states
 *   - Map exploration state
 *   - Multiple save slots
 *
 * Uses nlohmann/json if available, otherwise falls back to a simple
 * text-based format.
 */

#pragma once

#include "castle_command.hpp"
#include "game_date.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tensyoki {

/// Save slot metadata
struct SaveSlotInfo {
    int slot = 0;
    std::string save_name;
    std::string date_string;
    int32_t date_year = 1;
    int32_t date_month = 4;
    int32_t date_day = 1;
    int32_t player_castle_count = 0;
    int32_t total_castles = 0;
    bool is_valid = false;
};

/// Complete game state for saving/loading
struct GameSaveData {
    int32_t version = 1;  // Save format version

    // Game date
    int32_t date_year = 1;
    int32_t date_month = 4;
    int32_t date_day = 1;

    // Castle data
    std::vector<CastleData> castles;

    // Save metadata
    std::string save_name;
    int32_t player_castle_count = 0;
    int32_t total_castles = 0;
};

/// Save system manager
class SaveSystem {
public:
    SaveSystem() = default;
    ~SaveSystem() = default;

    SaveSystem(const SaveSystem&) = delete;
    SaveSystem& operator=(const SaveSystem&) = delete;

    /// Initialize save system with save directory path
    void init(const std::string& save_dir);

    /// Get list of available save slots
    std::vector<SaveSlotInfo> list_saves() const;

    /// Save game to a slot
    bool save(int slot, const GameSaveData& data);

    /// Load game from a slot
    bool load(int slot, GameSaveData& data);

    /// Delete a save slot
    bool delete_save(int slot);

    /// Check if a save slot exists
    bool slot_exists(int slot) const;

    /// Get save file path for a slot
    std::string get_save_path(int slot) const;

private:
    std::string save_dir_;

    /// Serialize GameSaveData to JSON string
    std::string serialize(const GameSaveData& data) const;

    /// Deserialize JSON string to GameSaveData
    bool deserialize(const std::string& json, GameSaveData& data) const;

    /// Ensure save directory exists
    bool ensure_dir() const;
};

} // namespace tensyoki
/**
 * castle_command.hpp - Castle management commands for Phase 4
 *
 * Provides commands available when a castle is selected on the strategic map:
 *   - View castle details
 *   - Appoint/dismiss officers
 *   - Recruit troops
 *   - Construct facilities
 *   - View garrison
 */

#pragma once

#include "game_date.hpp"
#include "game_state.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace tensyoki {

class SndataArchive;  // forward declaration

/// Castle facility types
enum class FacilityType {
    None,
    Wall,       // Increases defense
    Barracks,   // Increases recruitment capacity
    Storehouse, // Increases food storage
    Market,     // Increases gold income
};

/// Castle data
struct CastleData {
    uint32_t index = 0;
    std::string name;
    std::string name_utf8;

    // Castle stats
    int32_t level = 1;
    int32_t defense = 10;
    int32_t max_defense = 50;
    int32_t garrison = 0;
    int32_t max_garrison = 100;

    // Resources
    int32_t gold = 1000;
    int32_t food = 500;

    // Facilities
    bool has_wall = false;
    bool has_barracks = false;
    bool has_storehouse = false;
    bool has_market = false;

    // Assigned officer (castle lord)
    uint32_t lord_officer_id = 0;
    std::string lord_name;
    std::string lord_name_utf8;
};

/// Command result
enum class CommandResult {
    Success,
    InsufficientGold,
    InsufficientFood,
    NoOfficerAvailable,
    AlreadyBuilt,
    InvalidTarget,
};

/// Castle command manager: handles commands issued from the map view
class CastleCommand {
public:
    CastleCommand() = default;
    ~CastleCommand() = default;

    // Initialize with scenario data
    void init(const SndataArchive& sndata);

    // Get castle data by index
    const CastleData* get_castle(uint32_t index) const;
    CastleData* get_castle(uint32_t index);

    // Assign an officer as castle lord
    CommandResult assign_lord(uint32_t castle_index, uint32_t officer_id);

    // Dismiss (remove) the castle lord
    CommandResult dismiss_lord(uint32_t castle_index);

    // Recruit troops at a castle
    CommandResult recruit_troops(uint32_t castle_index, int32_t amount);

    // Auto-recruit: fill garrison up to max with available gold
    CommandResult auto_recruit(uint32_t castle_index);

    // Build a facility at a castle
    CommandResult build_facility(uint32_t castle_index, FacilityType type);

    // Upgrade castle level
    CommandResult upgrade_castle(uint32_t castle_index);

    // Get construction cost for a facility
    int32_t facility_cost(FacilityType type) const;

    // Get upgrade cost for castle
    int32_t castle_upgrade_cost(uint32_t castle_index) const;

    // Get recruitment cost per troop
    int32_t recruitment_cost_per_troop() const { return 5; }

    // Process monthly income/upkeep (called at month start)
    void process_monthly_income();

    // Get all castles
    const std::vector<CastleData>& castles() const { return castles_; }

    // Get all available officers (not assigned as lord anywhere)
    std::vector<uint32_t> get_available_officers(uint32_t exclude_castle_id = 0) const;

    // Get officer name by ID from sndata (helper for UI)
    std::string get_officer_name(uint32_t officer_id) const;

private:
    std::vector<CastleData> castles_;
    const SndataArchive* sndata_ = nullptr;  // not owned, set in init()
};

} // namespace tensyoki

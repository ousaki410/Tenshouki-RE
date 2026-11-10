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

    // Region control
    uint32_t region_id = 0;           // Which region this castle belongs to
    bool is_player_owned = false;     // Player controls this castle
    bool is_capital = false;          // Capital city (victory condition)
    int32_t auto_defense_bonus = 0;   // Auto-defense troops from facilities

    // Loyalty system
    int32_t loyalty = 50;             // Loyalty 0-100, affects rebellion chance
    int32_t tax_rate = 50;            // Tax rate 0-100, affects income and loyalty
    int32_t unrest = 0;               // Unrest 0-100, high unrest causes rebellion
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

    // Tax rate management
    int32_t tax_rate() const { return castle_tax_rate_; }
    void set_tax_rate(int32_t rate);
    int32_t calculate_tax_income(const CastleData& castle) const;

    // Loyalty management
    int32_t castle_loyalty(uint32_t castle_index) const;
    void adjust_loyalty(uint32_t castle_index, int32_t delta);
    int32_t calculate_loyalty_change(const CastleData& castle) const;

    // Rebellion system
    bool check_rebellion(uint32_t castle_index);
    void trigger_rebellion(uint32_t castle_index);

    // Resource production calculations
    int32_t calculate_gold_income(const CastleData& castle) const;
    int32_t calculate_food_production(const CastleData& castle) const;
    int32_t calculate_food_consumption(const CastleData& castle) const;
    int32_t calculate_auto_defense(const CastleData& castle) const;

    // Process monthly income/upkeep (called at month start)
    void process_monthly_income();

    // Process auto-recruitment for all player-owned castles
    void process_auto_recruit();

    // Process region control bonuses (called at month start)
    void process_region_control();

    // Get all castles
    const std::vector<CastleData>& castles() const { return castles_; }

    // Get castles owned by player
    std::vector<uint32_t> get_player_castles() const;

    // Get count of castles owned by player
    int32_t player_castle_count() const;

    // Rebellion events
    struct RebellionEvent {
        uint32_t castle_index = 0;
        std::string castle_name;
        int32_t turn = 0;
    };
    const std::vector<RebellionEvent>& rebellion_events() const { return rebellion_events_; }

    // Get rebellion message for UI display
    std::string get_latest_rebellion_message() const;

    // Get castles owned by player in a specific region
    std::vector<uint32_t> get_player_castles_in_region(uint32_t region_id) const;

    // Get all castles in a region
    std::vector<uint32_t> get_castles_in_region(uint32_t region_id) const;

    // Check if player controls all castles in a region
    bool controls_region(uint32_t region_id) const;

    // Get all available officers (not assigned as lord anywhere)
    std::vector<uint32_t> get_available_officers(uint32_t exclude_castle_id = 0) const;

    // Get officer name by ID from sndata (helper for UI)
    std::string get_officer_name(uint32_t officer_id) const;

    // Set all castle data (used for save/load)
    void set_castles_data(const std::vector<CastleData>& castles);

    // Get current turn number
    int32_t get_turn_count() const { return current_turn_; }

    // Advance turn counter
    void advance_turn();

private:
    std::vector<CastleData> castles_;
    const SndataArchive* sndata_ = nullptr;  // not owned, set in init()
    int32_t castle_tax_rate_ = 50;  // Global tax rate 0-100
    int32_t current_turn_ = 0;      // Current turn counter
    std::vector<RebellionEvent> rebellion_events_;  // Rebellion history
};

} // namespace tensyoki

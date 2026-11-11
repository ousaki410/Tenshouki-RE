/**
 * castle_command.cpp - Castle management command implementation
 */

#include "castle_command.hpp"
#include "core/logging.hpp"
#include "resource/dat_parser.hpp"
#include <algorithm>
#include <random>
#include <set>

namespace tensyoki {

void CastleCommand::init(const SndataArchive& sndata) {
    sndata_ = &sndata;
    castles_.clear();
    castles_.reserve(sndata.castle_count());

    for (const auto& castle : sndata.castles) {
        CastleData cd;
        cd.index = castle.index;
        cd.name = castle.name_raw;
        cd.name_utf8 = castle.name_utf8.empty() ? castle.name_raw
                                                : castle.name_utf8;
        cd.level = 1;
        cd.defense = 10;
        cd.max_defense = 50;
        // Start with a defensive garrison. Without it every castle would be
        // trivially captured by the first AI attack on turn one.
        cd.max_garrison = 100;
        cd.garrison = cd.max_garrison * 3 / 5;  // 60 troops
        cd.gold = 1000;
        cd.food = 500;
        cd.has_wall = false;
        cd.has_barracks = false;
        cd.has_storehouse = false;
        cd.has_market = false;
        cd.lord_officer_id = 0;

        // Default campaign setup: eight regions, one faction per region.
        // The player starts in Kansai (region 4); every region's first castle
        // is its capital.
        //
        // NOTE: the sndata castle record is only 33 bytes and carries no
        // explicit region field (verified across sndata1..6.dat), so this is
        // a temporary placeholder. Replace with a per-castle region table
        // recovered from scenario/map data when available.
        cd.region_id = castle.index % 8;
        cd.owner_id = default_owner_for_region(cd.region_id);
        cd.is_player_owned = (cd.owner_id == 1);
        cd.is_capital = (castle.index < 8);
        cd.tax_rate = 50;
        castles_.push_back(cd);
    }

    LOG_INFO("CastleCommand: initialized {} castles", castles_.size());
}

const CastleData* CastleCommand::get_castle(uint32_t index) const {
    for (const auto& c : castles_) {
        if (c.index == index) return &c;
    }
    return nullptr;
}

CastleData* CastleCommand::get_castle(uint32_t index) {
    for (auto& c : castles_) {
        if (c.index == index) return &c;
    }
    return nullptr;
}

CommandResult CastleCommand::assign_lord(uint32_t castle_index,
                                         uint32_t officer_id) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    // Find the officer
    // In a real implementation we'd look up the officer from sndata
    // For now, just assign
    castle->lord_officer_id = officer_id;
    LOG_INFO("CastleCommand: assigned officer {} to castle {}",
             officer_id, castle_index);
    return CommandResult::Success;
}

CommandResult CastleCommand::dismiss_lord(uint32_t castle_index) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    uint32_t old_lord = castle->lord_officer_id;
    castle->lord_officer_id = 0;
    castle->lord_name.clear();
    castle->lord_name_utf8.clear();

    LOG_INFO("CastleCommand: dismissed officer {} from castle {}",
             old_lord, castle_index);
    return CommandResult::Success;
}

CommandResult CastleCommand::recruit_troops(uint32_t castle_index,
                                            int32_t amount) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    int32_t cost = amount * recruitment_cost_per_troop();
    if (castle->gold < cost) return CommandResult::InsufficientGold;
    if (castle->garrison + amount > castle->max_garrison) {
        amount = castle->max_garrison - castle->garrison;
        if (amount <= 0) return CommandResult::InsufficientGold;
        cost = amount * recruitment_cost_per_troop();
        if (castle->gold < cost) return CommandResult::InsufficientGold;
    }

    castle->gold -= cost;
    castle->garrison += amount;
    LOG_INFO("CastleCommand: recruited {} troops at castle {} (cost: {} gold)",
             amount, castle_index, cost);
    return CommandResult::Success;
}

CommandResult CastleCommand::auto_recruit(uint32_t castle_index) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    int32_t needed = castle->max_garrison - castle->garrison;
    if (needed <= 0) return CommandResult::Success;  // Already full

    int32_t cost = needed * recruitment_cost_per_troop();
    if (castle->gold < cost) {
        // Recruit as many as possible with available gold
        int32_t affordable = castle->gold / recruitment_cost_per_troop();
        if (affordable <= 0) return CommandResult::InsufficientGold;
        needed = affordable;
        cost = needed * recruitment_cost_per_troop();
    }

    castle->gold -= cost;
    castle->garrison += needed;
    LOG_INFO("CastleCommand: auto-recruited {} troops at castle {} (cost: {} gold)",
             needed, castle_index, cost);
    return CommandResult::Success;
}

CommandResult CastleCommand::build_facility(uint32_t castle_index,
                                            FacilityType type) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    int32_t cost = facility_cost(type);

    // Check if already built
    switch (type) {
        case FacilityType::Wall:
            if (castle->has_wall) return CommandResult::AlreadyBuilt;
            break;
        case FacilityType::Barracks:
            if (castle->has_barracks) return CommandResult::AlreadyBuilt;
            break;
        case FacilityType::Storehouse:
            if (castle->has_storehouse) return CommandResult::AlreadyBuilt;
            break;
        case FacilityType::Market:
            if (castle->has_market) return CommandResult::AlreadyBuilt;
            break;
        case FacilityType::None:
            return CommandResult::InvalidTarget;
    }

    if (castle->gold < cost) return CommandResult::InsufficientGold;

    castle->gold -= cost;

    switch (type) {
        case FacilityType::Wall:
            castle->has_wall = true;
            castle->defense += 10;
            castle->max_defense += 10;
            break;
        case FacilityType::Barracks:
            castle->has_barracks = true;
            castle->max_garrison += 50;
            break;
        case FacilityType::Storehouse:
            castle->has_storehouse = true;
            break;
        case FacilityType::Market:
            castle->has_market = true;
            break;
        case FacilityType::None:
            break;
    }

    LOG_INFO("CastleCommand: built facility at castle {} (cost: {} gold)",
             castle_index, cost);
    return CommandResult::Success;
}

int32_t CastleCommand::facility_cost(FacilityType type) const {
    switch (type) {
        case FacilityType::Wall:      return 500;
        case FacilityType::Barracks:  return 300;
        case FacilityType::Storehouse:return 200;
        case FacilityType::Market:    return 400;
        case FacilityType::None:      return 0;
    }
    return 0;
}

void CastleCommand::set_tax_rate(int32_t rate) {
    castle_tax_rate_ = std::clamp(rate, 0, 100);
    // Keep per-castle tax rates in sync for player-owned castles so that
    // save files and the castle detail panel reflect the current policy.
    for (auto& castle : castles_) {
        if (castle.is_player_owned) {
            castle.tax_rate = castle_tax_rate_;
        }
    }
}

int32_t CastleCommand::calculate_tax_income(const CastleData& castle) const {
    int32_t base_income = 100;  // Base income
    if (castle.has_market) base_income += 200;
    if (castle.level > 1) base_income += 50 * (castle.level - 1);  // Level bonus

    // Tax rate affects income (higher rate = more gold but lower loyalty)
    int32_t income = base_income * castle_tax_rate_ / 100;

    return std::max(0, income);
}

int32_t CastleCommand::castle_loyalty(uint32_t castle_index) const {
    auto* castle = get_castle(castle_index);
    if (!castle) return 50;
    return castle->loyalty;
}

void CastleCommand::adjust_loyalty(uint32_t castle_index, int32_t delta) {
    auto* castle = get_castle(castle_index);
    if (!castle) return;

    castle->loyalty = std::clamp(castle->loyalty + delta, 0, 100);
    LOG_INFO("CastleCommand: castle {} loyalty changed by %d, new loyalty: {}",
             castle_index, delta, castle->loyalty);
}

int32_t CastleCommand::calculate_loyalty_change(const CastleData& castle) const {
    int32_t change = 0;

    // Tax rate affects loyalty (high tax = negative, low tax = positive)
    if (castle_tax_rate_ > 60) {
        change -= (castle_tax_rate_ - 60) / 10;
    } else if (castle_tax_rate_ < 40) {
        change += (40 - castle_tax_rate_) / 10;
    }

    // Garrison size affects loyalty (more troops = more stable)
    if (castle.garrison > castle.max_garrison / 2) {
        change += 1;
    } else if (castle.garrison < castle.max_garrison / 4) {
        change -= 2;
    }

    // Facilities affect loyalty
    if (castle.has_market) change += 1;
    if (castle.has_storehouse) change += 1;

    // Lord presence affects loyalty
    if (castle.lord_officer_id != 0) change += 2;

    return change;
}

bool CastleCommand::check_rebellion(uint32_t castle_index) {
    auto* castle = get_castle(castle_index);
    if (!castle || !castle->is_player_owned) return false;

    // High unrest or low loyalty increases rebellion chance
    int32_t rebellion_chance = 0;

    if (castle->unrest > 50) {
        rebellion_chance += (castle->unrest - 50) / 10;
    }
    if (castle->loyalty < 30) {
        rebellion_chance += (30 - castle->loyalty) / 5;
    }

    // Random chance
    std::mt19937 rng(castle->index * 1000 + castle->loyalty);
    std::uniform_int_distribution<int> dist(0, 100);

    if (dist(rng) < rebellion_chance) {
        trigger_rebellion(castle_index);
        return true;
    }

    return false;
}

void CastleCommand::trigger_rebellion(uint32_t castle_index) {
    auto* castle = get_castle(castle_index);
    if (!castle) return;

    castle->is_player_owned = false;
    castle->owner_id = 0;  // Rebels become independent
    castle->lord_officer_id = 0;
    castle->lord_name.clear();
    castle->lord_name_utf8.clear();
    castle->loyalty = 50;
    castle->unrest = 30;

    // Record rebellion event
    RebellionEvent event;
    event.castle_index = castle_index;
    event.castle_name = castle->name_utf8.empty() ? castle->name : castle->name_utf8;
    event.turn = current_turn_;
    rebellion_events_.push_back(event);

    LOG_WARN("CastleCommand: CASTLE {} REBELLED! Castle is now independent.",
             castle_index);
}

void CastleCommand::advance_turn() {
    current_turn_++;
}

int32_t CastleCommand::calculate_gold_income(const CastleData& castle) const {
    return calculate_tax_income(castle);
}

int32_t CastleCommand::calculate_food_production(const CastleData& castle) const {
    int32_t production = 50;  // Base food production
    if (castle.has_storehouse) production += 50;
    if (castle.level > 1) production += 25 * (castle.level - 1);
    return production;
}

int32_t CastleCommand::calculate_food_consumption(const CastleData& castle) const {
    int32_t consumption = castle.garrison / 10;
    if (consumption == 0) consumption = 1;
    return consumption;
}

int32_t CastleCommand::calculate_auto_defense(const CastleData& castle) const {
    int32_t defense_troops = 0;
    if (castle.has_wall) defense_troops += 30;
    if (castle.has_barracks) defense_troops += 20;
    defense_troops += castle.level * 10;
    return defense_troops;
}

void CastleCommand::process_monthly_income() {
    advance_turn();

    for (auto& castle : castles_) {
        if (!castle.is_player_owned) continue;  // Only process player-owned castles

        // Check for rebellion first
        if (check_rebellion(castle.index)) {
            continue;  // Castle rebelled, skip income
        }

        // Calculate loyalty change
        int32_t loyalty_change = calculate_loyalty_change(castle);
        castle.loyalty = std::clamp(castle.loyalty + loyalty_change, 0, 100);
        castle.unrest = std::clamp(castle.unrest - loyalty_change, 0, 100);

        // Gold income from tax
        int32_t income = calculate_tax_income(castle);
        castle.gold += income;

        // Food production and consumption
        int32_t production = calculate_food_production(castle);
        int32_t consumption = calculate_food_consumption(castle);
        castle.food = std::max(0, castle.food + production - consumption);

        LOG_DEBUG("CastleCommand: castle {} income {} gold, food prod {} cons {}, loyalty {}",
                  castle.index, income, production, consumption, castle.loyalty);
    }
}

void CastleCommand::process_auto_recruit() {
    for (auto& castle : castles_) {
        if (!castle.is_player_owned) continue;
        if (castle.garrison >= castle.max_garrison) continue;
        if (castle.gold < recruitment_cost_per_troop()) continue;

        int32_t needed = castle.max_garrison - castle.garrison;
        int32_t affordable = castle.gold / recruitment_cost_per_troop();
        int32_t to_recruit = std::min(needed, affordable);
        if (to_recruit <= 0) continue;

        castle.gold -= to_recruit * recruitment_cost_per_troop();
        castle.garrison += to_recruit;

        LOG_DEBUG("CastleCommand: auto-recruited {} troops at castle {}",
                  to_recruit, castle.index);
    }
}

void CastleCommand::process_region_control() {
    if (!sndata_) return;

    // Collect all region IDs
    std::set<uint32_t> region_ids;
    for (const auto& c : castles_) {
        region_ids.insert(c.region_id);
    }

    for (uint32_t region_id : region_ids) {
        if (controls_region(region_id)) {
            // Region control bonus: +100 gold and +50 food to all castles in region
            for (auto& castle : castles_) {
                if (castle.region_id == region_id && castle.is_player_owned) {
                    castle.gold += 100;
                    castle.food += 50;
                    LOG_DEBUG("CastleCommand: region control bonus for castle {}",
                              castle.index);
                }
            }
        }
    }
}

std::vector<uint32_t> CastleCommand::get_player_castles() const {
    std::vector<uint32_t> result;
    for (const auto& c : castles_) {
        if (c.is_player_owned) result.push_back(c.index);
    }
    return result;
}

int32_t CastleCommand::player_castle_count() const {
    int32_t count = 0;
    for (const auto& c : castles_) {
        if (c.is_player_owned) count++;
    }
    return count;
}

std::vector<uint32_t> CastleCommand::get_player_castles_in_region(
    uint32_t region_id) const {
    std::vector<uint32_t> result;
    for (const auto& c : castles_) {
        if (c.is_player_owned && c.region_id == region_id) {
            result.push_back(c.index);
        }
    }
    return result;
}

std::vector<uint32_t> CastleCommand::get_castles_in_region(
    uint32_t region_id) const {
    std::vector<uint32_t> result;
    for (const auto& c : castles_) {
        if (c.region_id == region_id) result.push_back(c.index);
    }
    return result;
}

bool CastleCommand::controls_region(uint32_t region_id) const {
    auto region_castles = get_castles_in_region(region_id);
    if (region_castles.empty()) return false;
    for (uint32_t idx : region_castles) {
        if (!get_castle(idx)->is_player_owned) return false;
    }
    return true;
}

void CastleCommand::set_castles_data(const std::vector<CastleData>& castles) {
    castles_ = castles;
}

CommandResult CastleCommand::upgrade_castle(uint32_t castle_index) {
    auto* castle = get_castle(castle_index);
    if (!castle) return CommandResult::InvalidTarget;

    int32_t cost = castle_upgrade_cost(castle_index);
    if (castle->gold < cost) return CommandResult::InsufficientGold;

    castle->gold -= cost;
    castle->level += 1;
    castle->max_defense += 20;
    castle->defense = castle->max_defense;  // Restore to full
    castle->max_garrison += 30;

    LOG_INFO("CastleCommand: upgraded castle {} to level {} (cost: {} gold)",
             castle_index, castle->level, cost);
    return CommandResult::Success;
}

int32_t CastleCommand::castle_upgrade_cost(uint32_t castle_index) const {
    auto* castle = get_castle(castle_index);
    if (!castle) return 0;
    return 500 * castle->level;  // Cost scales with level
}

std::vector<uint32_t> CastleCommand::get_available_officers(
    uint32_t exclude_castle_id) const {
    std::vector<uint32_t> available;

    if (!sndata_) return available;

    // Collect officer IDs already assigned as lords (exclude the given castle)
    std::set<uint32_t> assigned_ids;
    for (const auto& c : castles_) {
        if (c.index == exclude_castle_id) continue;
        if (c.lord_officer_id != 0) {
            assigned_ids.insert(c.lord_officer_id);
        }
    }

    // Find officers from sndata who are not assigned as lords
    for (const auto& officer : sndata_->officers) {
        // Skip empty slots (id == 0xFFFF)
        if (officer.id == 0xFFFF) continue;
        // Skip already assigned officers
        if (assigned_ids.count(officer.id)) continue;
        available.push_back(officer.id);
    }

    return available;
}

std::string CastleCommand::get_officer_name(uint32_t officer_id) const {
    if (!sndata_) return "";

    for (const auto& officer : sndata_->officers) {
        if (officer.id == officer_id) {
            return officer.name_utf8.empty() ? officer.name_raw
                                             : officer.name_utf8;
        }
    }
    return "";
}

std::string CastleCommand::get_latest_rebellion_message() const {
    if (rebellion_events_.empty()) return "";

    const auto& latest = rebellion_events_.back();
    return fmt::format("TURN {}: Castle {} ({}) REBELLED! It is now independent.",
                       latest.turn, latest.castle_index, latest.castle_name);
}

} // namespace tensyoki

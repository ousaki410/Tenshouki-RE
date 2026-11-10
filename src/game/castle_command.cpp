/**
 * castle_command.cpp - Castle management command implementation
 */

#include "castle_command.hpp"
#include "core/logging.hpp"
#include "resource/dat_parser.hpp"
#include <algorithm>
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
        cd.garrison = 0;
        cd.max_garrison = 100;
        cd.gold = 1000;
        cd.food = 500;
        cd.has_wall = false;
        cd.has_barracks = false;
        cd.has_storehouse = false;
        cd.has_market = false;
        cd.lord_officer_id = 0;
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

int32_t CastleCommand::calculate_gold_income(const CastleData& castle) const {
    int32_t income = 100;  // Base income
    if (castle.has_market) income += 200;
    if (castle.level > 1) income += 50 * (castle.level - 1);  // Level bonus
    return income;
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
    for (auto& castle : castles_) {
        if (!castle.is_player_owned) continue;  // Only process player-owned castles

        // Gold income
        int32_t income = calculate_gold_income(castle);
        castle.gold += income;

        // Food production and consumption
        int32_t production = calculate_food_production(castle);
        int32_t consumption = calculate_food_consumption(castle);
        castle.food = std::max(0, castle.food + production - consumption);

        LOG_DEBUG("CastleCommand: castle {} income {} gold, food prod {} cons {}",
                  castle.index, income, production, consumption);
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

} // namespace tensyoki

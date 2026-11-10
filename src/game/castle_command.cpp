/**
 * castle_command.cpp - Castle management command implementation
 */

#include "castle_command.hpp"
#include "core/logging.hpp"
#include "resource/dat_parser.hpp"
#include <algorithm>

namespace tensyoki {

void CastleCommand::init(const SndataArchive& sndata) {
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

void CastleCommand::process_monthly_income() {
    for (auto& castle : castles_) {
        // Gold income from market and base
        int32_t income = 100;
        if (castle.has_market) income += 200;
        castle.gold += income;

        // Food consumption by garrison
        int32_t consumption = castle.garrison / 10;
        if (consumption == 0) consumption = 1;
        castle.food = std::max(0, castle.food - consumption);

        LOG_DEBUG("CastleCommand: castle {} income {} gold, food {}",
                  castle.index, income, castle.food);
    }
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
    // In a full implementation, this would query the officer database
    // and exclude officers already assigned as lords
    for (const auto& c : castles_) {
        if (c.index == exclude_castle_id) continue;
        if (c.lord_officer_id != 0) {
            // Officer already assigned, add as available if not lord here
            // (simplified: just track assigned officer IDs)
        }
    }
    // Return empty for now - in a full implementation, this would query
    // the officer database for unassigned officers
    return available;
}

} // namespace tensyoki

/**
 * battle.cpp - Tactical battle system implementation
 */

#include "battle.hpp"
#include "core/logging.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace tensyoki {

BattleStateData::BattleStateData() = default;

void BattleStateData::init_battle() {
    units_.clear();
    terrain_grid_.assign(static_cast<size_t>(grid_width_) * grid_height_,
                         TerrainCell{});

    // Generate terrain: mostly plains with some forests and mountains
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> terrain_roll(0, 99);

    for (int32_t y = 0; y < grid_height_; ++y) {
        for (int32_t x = 0; x < grid_width_; ++x) {
            auto& cell = terrain_grid_[idx(x, y)];
            int roll = terrain_roll(rng);
            if (roll < 10) {
                cell.type = TerrainType::Forest;
            } else if (roll < 15) {
                cell.type = TerrainType::Mountain;
            } else if (roll < 20) {
                cell.type = TerrainType::Water;
            } else {
                cell.type = TerrainType::Plain;
            }
        }
    }

    // Place a castle in the center
    int32_t cx = grid_width_ / 2;
    int32_t cy = grid_height_ / 2;
    terrain_grid_[idx(cx, cy)].type = TerrainType::Castle;

    // Create player units (around left side)
    BattleUnit p1;
    p1.id = next_unit_id_++;
    p1.officer_id = 1;
    p1.name = "Player Unit 1";
    p1.is_player = true;
    p1.hp = p1.max_hp = 120;
    p1.attack = 25;
    p1.defense = 15;
    p1.move = 3;
    p1.move_left = 3;
    p1.grid_x = 3;
    p1.grid_y = grid_height_ / 2;
    units_.push_back(p1);

    BattleUnit p2;
    p2.id = next_unit_id_++;
    p2.officer_id = 2;
    p2.name = "Player Unit 2";
    p2.is_player = true;
    p2.hp = p2.max_hp = 100;
    p2.attack = 20;
    p2.defense = 12;
    p2.move = 4;
    p2.move_left = 4;
    p2.grid_x = 3;
    p2.grid_y = grid_height_ / 2 + 2;
    units_.push_back(p2);

    // Create enemy units (around right side)
    BattleUnit e1;
    e1.id = next_unit_id_++;
    e1.officer_id = 10;
    e1.name = "Enemy Unit 1";
    e1.is_player = false;
    e1.hp = e1.max_hp = 110;
    e1.attack = 22;
    e1.defense = 13;
    e1.move = 3;
    e1.move_left = 3;
    e1.grid_x = grid_width_ - 4;
    e1.grid_y = grid_height_ / 2;
    units_.push_back(e1);

    BattleUnit e2;
    e2.id = next_unit_id_++;
    e2.officer_id = 11;
    e2.name = "Enemy Unit 2";
    e2.is_player = false;
    e2.hp = e2.max_hp = 90;
    e2.attack = 18;
    e2.defense = 11;
    e2.move = 3;
    e2.move_left = 3;
    e2.grid_x = grid_width_ - 4;
    e2.grid_y = grid_height_ / 2 + 2;
    units_.push_back(e2);

    // Register units on terrain grid
    for (auto& u : units_) {
        if (is_valid_position(u.grid_x, u.grid_y)) {
            terrain_grid_[idx(u.grid_x, u.grid_y)].unit_id = u.id;
        }
    }

    LOG_INFO("BattleStateData: battle initialized with {} units",
             units_.size());
}

BattleUnit* BattleStateData::get_unit_at(int32_t x, int32_t y) {
    if (!is_valid_position(x, y)) return nullptr;
    uint32_t uid = terrain_grid_[idx(x, y)].unit_id;
    if (uid == 0) return nullptr;
    for (auto& u : units_) {
        if (u.id == uid) return &u;
    }
    return nullptr;
}

const BattleUnit* BattleStateData::get_unit_at(int32_t x, int32_t y) const {
    if (!is_valid_position(x, y)) return nullptr;
    uint32_t uid = terrain_grid_[idx(x, y)].unit_id;
    if (uid == 0) return nullptr;
    for (const auto& u : units_) {
        if (u.id == uid) return &u;
    }
    return nullptr;
}

TerrainCell& BattleStateData::get_terrain(int32_t x, int32_t y) {
    return terrain_grid_[idx(x, y)];
}

const TerrainCell& BattleStateData::get_terrain(int32_t x, int32_t y) const {
    return terrain_grid_[idx(x, y)];
}

bool BattleStateData::is_valid_position(int32_t x, int32_t y) const {
    return x >= 0 && x < grid_width_ && y >= 0 && y < grid_height_;
}

bool BattleStateData::is_position_empty(int32_t x, int32_t y) const {
    if (!is_valid_position(x, y)) return false;
    return terrain_grid_[idx(x, y)].unit_id == 0;
}

int32_t BattleStateData::movement_cost(int32_t x, int32_t y) const {
    if (!is_valid_position(x, y)) return 999;
    auto t = terrain_grid_[idx(x, y)].type;
    switch (t) {
        case TerrainType::Plain:    return 1;
        case TerrainType::Forest:   return 2;
        case TerrainType::Mountain: return 3;
        case TerrainType::Water:    return 4;
        case TerrainType::Castle:   return 1;
    }
    return 1;
}

int32_t BattleStateData::defense_bonus(int32_t x, int32_t y) const {
    if (!is_valid_position(x, y)) return 0;
    auto t = terrain_grid_[idx(x, y)].type;
    switch (t) {
        case TerrainType::Plain:    return 0;
        case TerrainType::Forest:   return 1;
        case TerrainType::Mountain: return 2;
        case TerrainType::Water:    return -1;
        case TerrainType::Castle:   return 3;
    }
    return 0;
}

int32_t BattleStateData::roll_damage(int32_t attack, int32_t defense) const {
    std::mt19937 rng(42 + attack * 7 + defense * 13);
    std::uniform_int_distribution<int> roll(0, 20);
    int32_t base = attack - defense + roll(rng);
    return std::max(0, base);
}

bool BattleStateData::attack(BattleUnit& attacker, BattleUnit& defender) {
    if (attacker.has_acted) return false;
    if (defender.is_dead()) return false;

    int32_t dmg = roll_damage(attacker.attack, defender.defense +
                              defense_bonus(defender.grid_x, defender.grid_y));
    defender.hp = std::max(0, defender.hp - dmg);
    attacker.has_acted = true;
    attacker.move_left = 0;

    LOG_INFO("Battle: {} attacks {} for {} damage (defender HP: {})",
             attacker.name, defender.name, dmg, defender.hp);

    if (defender.is_dead()) {
        // Remove defender from terrain grid
        terrain_grid_[idx(defender.grid_x, defender.grid_y)].unit_id = 0;
        LOG_INFO("Battle: {} defeated!", defender.name);

        // Check win condition
        bool any_enemy_alive = false;
        bool any_player_alive = false;
        for (const auto& u : units_) {
            if (u.is_dead()) continue;
            if (u.is_player) any_player_alive = true;
            else any_enemy_alive = true;
        }
        if (!any_player_alive) {
            result_ = BattleResult::PlayerDefeat;
        } else if (!any_enemy_alive) {
            result_ = BattleResult::PlayerVictory;
        }
    }
    return true;
}

bool BattleStateData::is_in_attack_range(int32_t ax, int32_t ay,
                                         int32_t tx, int32_t ty) const {
    return std::abs(ax - tx) + std::abs(ay - ty) <= 1;
}

void BattleStateData::next_turn() {
    is_player_turn_ = !is_player_turn_;

    // Reset action states for all units of the new turn's side
    for (auto& u : units_) {
        if (u.is_dead()) continue;
        if ((u.is_player && is_player_turn_) ||
            (!u.is_player && !is_player_turn_)) {
            u.has_acted = false;
            u.move_left = u.move;
        }
    }

    // Check win conditions after turn switch
    bool any_player_alive = false;
    bool any_enemy_alive = false;
    for (const auto& u : units_) {
        if (u.is_dead()) continue;
        if (u.is_player) any_player_alive = true;
        else any_enemy_alive = true;
    }
    if (!any_player_alive) {
        result_ = BattleResult::PlayerDefeat;
    } else if (!any_enemy_alive) {
        result_ = BattleResult::PlayerVictory;
    }
}

bool BattleStateData::all_player_units_acted() const {
    for (const auto& u : units_) {
        if (u.is_dead() || !u.is_player) continue;
        if (!u.has_acted) return false;
    }
    return true;
}

bool BattleStateData::all_enemy_units_acted() const {
    for (const auto& u : units_) {
        if (u.is_dead() || u.is_player) continue;
        if (!u.has_acted) return false;
    }
    return true;
}

} // namespace tensyoki

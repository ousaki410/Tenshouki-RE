/**
 * battle.cpp - Tactical battle system implementation
 *
 * Turn-based tactical combat on a grid map with units, terrain, and combat
 * resolution.
 *
 * Extensions:
 *   - Unit types: Infantry, Cavalry, Archer (different move/range/defense)
 *   - Morale system: affects effective attack and defense
 *   - Ranged attacks: Archers can attack 2-3 tiles away
 *   - Battle event log: detailed combat history
 *   - Improved AI: prioritizes weak enemies, flanking tactics
 *   - Terrain-based combat modifiers
 *   - Status effects: Silence, Poison, Counterattack, Sleep, Slow, Weakness, ArmorBreak, Burn
 *   - Skills: Heal, MoraleBoost, PowerStrike, Guard, Inspire, RainOfArrows, Silence, Poison, Counterattack, Sleep, Slow, Weakness, ArmorBreak, Burn, HealAll, InspireAll, TacticalRetreat
 *   - Weather system: Clear, Rain, Snow, Fog
 *   - Routing: Enemy units with low morale may flee
 */

#include "battle.hpp"
#include "core/logging.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <queue>
#include <set>

namespace tensyoki {

BattleStateData::BattleStateData() = default;

std::vector<BattleSkill> BattleUnit::get_available_skills() const {
    std::vector<BattleSkill> skills;

    // All units can use Guard at level 1
    skills.push_back({SkillType::Guard, "Guard",
                      "Temporarily boost defense this turn.", 10, 1,
                      false, false, 1});

    // Heal - available at level 3+
    if (level >= 3) {
        skills.push_back({SkillType::Heal, "Heal",
                          "Restore HP to self or adjacent ally.", 25, 1,
                          false, false, 1});
    }

    // MoraleBoost - available at level 2+
    if (level >= 2) {
        skills.push_back({SkillType::MoraleBoost, "Morale Boost",
                          "Restore morale to self or adjacent ally.", 15, 1,
                          false, false, 1});
    }

    // PowerStrike - available at level 4+, melee units only
    if (level >= 4 && unit_type != UnitType::Archer) {
        skills.push_back({SkillType::PowerStrike, "Power Strike",
                          "Strong melee attack with high damage.", 30, 1,
                          true, false, 1});
    }

    // Inspire - available at level 5+, Infantry/Cavalry only
    if (level >= 5 && unit_type != UnitType::Archer) {
        skills.push_back({SkillType::Inspire, "Inspire",
                          "Boost attack of adjacent allies.", 35, 1,
                          false, false, 1});
    }

    // RainOfArrows - Archer only, level 6+
    if (level >= 6 && unit_type == UnitType::Archer) {
        skills.push_back({SkillType::RainOfArrows, "Rain of Arrows",
                          "Damage all enemies in range 3.", 50, 3,
                          true, true, 3});
    }

    // Silence - available at level 4+, prevents enemy skill use
    if (level >= 4) {
        skills.push_back({SkillType::Silence, "Silence",
                          "Enemy cannot use skills for 2 turns.", 25, 2,
                          true, false, 2});
    }

    // Poison - available at level 5+, damage over time
    if (level >= 5) {
        skills.push_back({SkillType::Poison, "Poison",
                          "Enemy takes damage each turn for 3 turns.", 20, 2,
                          true, false, 2});
    }

    // Counterattack - available at level 3+, auto-attack when attacked
    if (level >= 3) {
        skills.push_back({SkillType::Counterattack, "Counterattack",
                          "Auto-counterattack when attacked for 2 turns.", 15, 1,
                          false, false, 1});
    }

    // Sleep - available at level 6+, enemy cannot act for 2 turns
    if (level >= 6) {
        skills.push_back({SkillType::Sleep, "Sleep",
                          "Enemy cannot act for 2 turns.", 40, 2,
                          true, false, 2});
    }

    // Slow - available at level 5+, enemy movement halved for 2 turns
    if (level >= 5) {
        skills.push_back({SkillType::Slow, "Slow",
                          "Enemy movement halved for 2 turns.", 25, 2,
                          true, false, 2});
    }

    // Weakness - available at level 4+, enemy attack reduced for 3 turns
    if (level >= 4) {
        skills.push_back({SkillType::Weakness, "Weakness",
                          "Enemy attack reduced for 3 turns.", 20, 2,
                          true, false, 3});
    }

    // ArmorBreak - available at level 5+, enemy defense reduced for 3 turns
    if (level >= 5) {
        skills.push_back({SkillType::ArmorBreak, "Armor Break",
                          "Enemy defense reduced for 3 turns.", 30, 2,
                          true, false, 3});
    }

    // Burn - available at level 7+, enemy takes fire damage for 3 turns
    if (level >= 7) {
        skills.push_back({SkillType::Burn, "Burn",
                          "Enemy takes fire damage each turn for 3 turns.", 35, 2,
                          true, false, 3});
    }

    // HealAll - Leader only, level 8+, heal all adjacent allies
    if (level >= 8 && unit_type != UnitType::Archer) {
        skills.push_back({SkillType::HealAll, "Heal All",
                          "Heal all adjacent allies.", 60, 2,
                          false, true, 2});
    }

    // InspireAll - Leader only, level 9+, boost morale of all allies
    if (level >= 9 && unit_type != UnitType::Archer) {
        skills.push_back({SkillType::InspireAll, "Inspire All",
                          "Boost morale of all allies.", 50, 3,
                          false, true, 3});
    }

    // TacticalRetreat - available at level 4+, move and gain defense
    if (level >= 4) {
        skills.push_back({SkillType::TacticalRetreat, "Tactical Retreat",
                          "Move and gain defensive bonus.", 20, 3,
                          false, false, 3});
    }

    return skills;
}

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

    // Initialize weather
    change_weather();

    // Create player units (around left side)
    BattleUnit p1;
    p1.id = next_unit_id_++;
    p1.officer_id = 1;
    p1.name = "Oda Infantry";
    p1.is_player = true;
    p1.unit_type = UnitType::Infantry;
    p1.hp = p1.max_hp = 120;
    p1.attack = 25;
    p1.defense = 15;
    p1.move = 3;
    p1.move_left = 3;
    p1.attack_range = 1;
    p1.morale = 60;
    p1.grid_x = 3;
    p1.grid_y = grid_height_ / 2;
    units_.push_back(p1);

    BattleUnit p2;
    p2.id = next_unit_id_++;
    p2.officer_id = 2;
    p2.name = "Oda Archer";
    p2.is_player = true;
    p2.unit_type = UnitType::Archer;
    p2.hp = p2.max_hp = 80;
    p2.attack = 18;
    p2.defense = 8;
    p2.move = 2;
    p2.move_left = 2;
    p2.attack_range = 3;
    p2.morale = 55;
    p2.grid_x = 3;
    p2.grid_y = grid_height_ / 2 + 2;
    units_.push_back(p2);

    BattleUnit p3;
    p3.id = next_unit_id_++;
    p3.officer_id = 3;
    p3.name = "Oda Cavalry";
    p3.is_player = true;
    p3.unit_type = UnitType::Cavalry;
    p3.hp = p3.max_hp = 100;
    p3.attack = 22;
    p3.defense = 10;
    p3.move = 5;
    p3.move_left = 5;
    p3.attack_range = 1;
    p3.morale = 50;
    p3.grid_x = 5;
    p3.grid_y = grid_height_ / 2 - 1;
    units_.push_back(p3);

    // Create enemy units (around right side)
    BattleUnit e1;
    e1.id = next_unit_id_++;
    e1.officer_id = 10;
    e1.name = "Enemy Infantry";
    e1.is_player = false;
    e1.unit_type = UnitType::Infantry;
    e1.hp = e1.max_hp = 110;
    e1.attack = 22;
    e1.defense = 13;
    e1.move = 3;
    e1.move_left = 3;
    e1.attack_range = 1;
    e1.morale = 50;
    e1.grid_x = grid_width_ - 4;
    e1.grid_y = grid_height_ / 2;
    units_.push_back(e1);

    BattleUnit e2;
    e2.id = next_unit_id_++;
    e2.officer_id = 11;
    e2.name = "Enemy Archer";
    e2.is_player = false;
    e2.unit_type = UnitType::Archer;
    e2.hp = e2.max_hp = 75;
    e2.attack = 20;
    e2.defense = 7;
    e2.move = 2;
    e2.move_left = 2;
    e2.attack_range = 3;
    e2.morale = 45;
    e2.grid_x = grid_width_ - 4;
    e2.grid_y = grid_height_ / 2 + 2;
    units_.push_back(e2);

    BattleUnit e3;
    e3.id = next_unit_id_++;
    e3.officer_id = 12;
    e3.name = "Enemy Cavalry";
    e3.is_player = false;
    e3.unit_type = UnitType::Cavalry;
    e3.hp = e3.max_hp = 95;
    e3.attack = 24;
    e3.defense = 9;
    e3.move = 5;
    e3.move_left = 5;
    e3.attack_range = 1;
    e3.morale = 55;
    e3.grid_x = grid_width_ - 6;
    e3.grid_y = grid_height_ / 2 - 1;
    units_.push_back(e3);

    // Register units on terrain grid
    for (auto& u : units_) {
        if (is_valid_position(u.grid_x, u.grid_y)) {
            terrain_grid_[idx(u.grid_x, u.grid_y)].unit_id = u.id;
        }
    }

    add_event("Battle started!", true);

    LOG_INFO("BattleStateData: battle initialized with {} units, weather: {}",
             units_.size(), weather_);
}

void BattleStateData::init_from_castle(uint32_t castle_index, bool is_attacker) {
    units_.clear();
    terrain_grid_.assign(static_cast<size_t>(grid_width_) * grid_height_,
                         TerrainCell{});

    // Generate terrain
    std::mt19937 rng(42 + castle_index);
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

    // Place castle in center for defender
    int32_t cx = grid_width_ / 2;
    int32_t cy = grid_height_ / 2;
    terrain_grid_[idx(cx, cy)].type = TerrainType::Castle;

    // Create units based on castle index as seed
    int32_t seed = static_cast<int32_t>(castle_index);
    int32_t garrison_size = 1 + (seed % 3);  // 1-3 defender units
    int32_t attacker_size = 2;  // Player always brings 2 units

    // Create defender units (castle garrison) - around castle
    for (int32_t i = 0; i < garrison_size; ++i) {
        BattleUnit u;
        u.id = next_unit_id_++;
        u.officer_id = 100 + seed * 10 + i;
        u.name = "Defender " + std::to_string(i + 1);
        u.is_player = false;
        u.unit_type = static_cast<UnitType>(i % 3);

        // Scale stats by castle level
        int32_t level_bonus = seed % 5;
        u.hp = u.max_hp = 90 + level_bonus * 10;
        u.attack = 18 + level_bonus * 3;
        u.defense = 12 + level_bonus * 2;
        u.move = 3;
        u.move_left = 3;
        u.attack_range = (u.unit_type == UnitType::Archer) ? 3 : 1;
        u.morale = 50 + (seed % 20);

        // Position around the castle
        u.grid_x = cx + (i - garrison_size / 2);
        u.grid_y = cy + 2;
        if (u.grid_x < 0) u.grid_x = 0;
        if (u.grid_x >= grid_width_) u.grid_x = grid_width_ - 1;
        if (u.grid_y >= grid_height_) u.grid_y = grid_height_ - 1;

        units_.push_back(u);
    }

    // Create attacker units (player) - around left side
    for (int32_t i = 0; i < attacker_size; ++i) {
        BattleUnit u;
        u.id = next_unit_id_++;
        u.officer_id = 1 + i;
        u.name = "Player Unit " + std::to_string(i + 1);
        u.is_player = true;
        u.unit_type = (i == 0) ? UnitType::Infantry : UnitType::Archer;

        u.hp = u.max_hp = 120;
        u.attack = 25;
        u.defense = 15;
        u.move = 3;
        u.move_left = 3;
        u.attack_range = (u.unit_type == UnitType::Archer) ? 3 : 1;
        u.morale = 60;

        u.grid_x = 3;
        u.grid_y = grid_height_ / 2 + i * 2;

        units_.push_back(u);
    }

    // Register units on terrain grid
    for (auto& u : units_) {
        if (is_valid_position(u.grid_x, u.grid_y)) {
            terrain_grid_[idx(u.grid_x, u.grid_y)].unit_id = u.id;
        }
    }

    add_event("Battle started at castle " + std::to_string(castle_index) + "!", true);

    LOG_INFO("BattleStateData: battle initialized from castle {} with {} units",
             castle_index, units_.size());
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

int32_t BattleStateData::movement_cost(int32_t x, int32_t y, const BattleUnit* unit) const {
    if (!is_valid_position(x, y)) return 999;
    auto t = terrain_grid_[idx(x, y)].type;
    int32_t cost = unit ? unit->get_terrain_move_cost(t) : 1;
    cost += weather_movement_penalty();
    return std::max(1, cost);
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

int32_t BattleStateData::attack(BattleUnit& attacker, BattleUnit& defender) {
    if (attacker.has_acted) return 0;
    if (defender.is_dead()) return 0;

    // Melee attack: must be adjacent
    int32_t dist = std::abs(attacker.grid_x - defender.grid_x) +
                   std::abs(attacker.grid_y - defender.grid_y);
    if (dist > 1) return 0;

    int32_t eff_atk = attacker.effective_attack();
    int32_t eff_def = defender.effective_defense() +
                      defender.get_terrain_defense_bonus(
                          terrain_grid_[idx(defender.grid_x, defender.grid_y)].type);
    int32_t atk_terrain_bonus = attacker.get_terrain_attack_bonus(
        terrain_grid_[idx(attacker.grid_x, attacker.grid_y)].type);
    eff_atk += atk_terrain_bonus;
    eff_atk += weather_attack_penalty();
    int32_t dmg = roll_damage(eff_atk, eff_def);
    defender.hp = std::max(0, defender.hp - dmg);
    attacker.has_acted = true;
    attacker.move_left = 0;

    // Morale changes: attacker gains, defender loses
    attacker.morale = std::min(attacker.max_morale, attacker.morale + 5);
    defender.morale = std::max(0, defender.morale - 10);

    add_event(fmt::format("{} attacks {} for {} dmg! {} HP left",
                          attacker.name, defender.name, dmg, defender.hp),
              attacker.is_player, dmg);

    // Counterattack: defender auto-attacks if has Counterattack status
    if (defender.has_counterattack() && !defender.is_dead()) {
        int32_t dist = std::abs(defender.grid_x - attacker.grid_x) +
                       std::abs(defender.grid_y - attacker.grid_y);
        if (dist <= 1) {
            int32_t cdmg = roll_damage(defender.effective_attack(),
                                       attacker.effective_defense() +
                                       defender.get_terrain_defense_bonus(
                                           terrain_grid_[idx(attacker.grid_x, attacker.grid_x)].type));
            attacker.hp = std::max(0, attacker.hp - cdmg);
            add_event(fmt::format("{} counterattacks {} for {} dmg!",
                                  defender.name, attacker.name, cdmg),
                      !defender.is_player, cdmg);
            if (attacker.is_dead()) {
                terrain_grid_[idx(attacker.grid_x, attacker.grid_y)].unit_id = 0;
                add_event(fmt::format("{} defeated!", attacker.name),
                          !attacker.is_player);
                check_win_conditions();
            }
        }
    }

    if (defender.is_dead()) {
        // Remove defender from terrain grid
        terrain_grid_[idx(defender.grid_x, defender.grid_y)].unit_id = 0;
        add_event(fmt::format("{} defeated!", defender.name),
                  attacker.is_player);

        // Grant experience to attacker
        if (attacker.is_player) {
            bool leveled = grant_exp(attacker, 30);
            if (leveled) {
                add_event(fmt::format("{} leveled up to Lv.{}!",
                                      attacker.name, attacker.level),
                          true);
            }
        }

        check_win_conditions();
    }
    return dmg;
}

int32_t BattleStateData::ranged_attack(BattleUnit& attacker,
                                        BattleUnit& defender) {
    if (attacker.has_acted) return 0;
    if (defender.is_dead()) return 0;

    // Ranged attack: range 2-3 tiles
    int32_t dist = std::abs(attacker.grid_x - defender.grid_x) +
                   std::abs(attacker.grid_y - defender.grid_y);
    if (dist < 2 || dist > 3) return 0;

    // Ranged attacks do slightly less damage
    int32_t eff_atk = attacker.effective_attack() - 5;
    int32_t eff_def = defender.effective_defense() +
                      defender.get_terrain_defense_bonus(
                          terrain_grid_[idx(defender.grid_x, defender.grid_y)].type);
    int32_t atk_terrain_bonus = attacker.get_terrain_attack_bonus(
        terrain_grid_[idx(attacker.grid_x, attacker.grid_y)].type);
    eff_atk += atk_terrain_bonus;
    int32_t dmg = roll_damage(eff_atk, eff_def);
    defender.hp = std::max(0, defender.hp - dmg);
    attacker.has_acted = true;
    attacker.move_left = 0;

    // Morale changes
    attacker.morale = std::min(attacker.max_morale, attacker.morale + 3);
    defender.morale = std::max(0, defender.morale - 5);

    add_event(fmt::format("{} shoots {} for {} dmg! {} HP left",
                          attacker.name, defender.name, dmg, defender.hp),
              attacker.is_player, dmg);

    if (defender.is_dead()) {
        terrain_grid_[idx(defender.grid_x, defender.grid_y)].unit_id = 0;
        add_event(fmt::format("{} defeated!", defender.name),
                  attacker.is_player);

        // Grant experience to attacker
        if (attacker.is_player) {
            bool leveled = grant_exp(attacker, 30);
            if (leveled) {
                add_event(fmt::format("{} leveled up to Lv.{}!",
                                      attacker.name, attacker.level),
                          true);
            }
        }

        check_win_conditions();
    }
    return dmg;
}

bool BattleStateData::is_in_attack_range(int32_t ax, int32_t ay,
                                          int32_t tx, int32_t ty,
                                          const BattleUnit& attacker) const {
    int32_t dist = std::abs(ax - tx) + std::abs(ay - ty);
    return dist <= attacker.get_attack_range();
}

bool BattleStateData::is_in_ranged_range(int32_t ax, int32_t ay,
                                          int32_t tx, int32_t ty) const {
    int32_t dist = std::abs(ax - tx) + std::abs(ay - ty);
    return dist >= 2 && dist <= 3;
}

void BattleStateData::add_event(const std::string& message,
                                 bool is_player_action, int32_t damage,
                                 bool is_skill) {
    BattleEvent evt;
    evt.message = message;
    evt.is_player_action = is_player_action;
    evt.damage = damage;
    evt.is_skill = is_skill;
    events_.push_back(evt);

    // Keep log size manageable
    if (events_.size() > 100) {
        events_.erase(events_.begin());
    }
}

void BattleStateData::check_win_conditions() {
    bool any_player_alive = false;
    bool any_enemy_alive = false;
    for (const auto& u : units_) {
        if (u.is_dead()) continue;
        if (u.is_player) any_player_alive = true;
        else any_enemy_alive = true;
    }
    if (!any_player_alive) {
        result_ = BattleResult::PlayerDefeat;
        add_event("Player defeated! Battle lost.", false);
        build_settlement();
    } else if (!any_enemy_alive) {
        result_ = BattleResult::PlayerVictory;
        add_event("Victory! All enemies defeated.", true);
        build_settlement();
    }
}

void BattleStateData::build_settlement() {
    settlement_.result = result_;
    settlement_.total_exp_gained = 0;
    settlement_.total_gold_gained = 0;
    settlement_.enemies_defeated = 0;
    settlement_.enemies_captured = 0;
    settlement_.player_units_lost = 0;

    for (const auto& u : units_) {
        if (u.is_player && u.is_dead()) {
            settlement_.player_units_lost++;
        }
        if (!u.is_player && u.is_dead()) {
            settlement_.enemies_defeated++;
            settlement_.total_exp_gained += 30;
            settlement_.total_gold_gained += 50;
            // 30% chance to capture officer
            std::mt19937 rng(u.id * 7 + 13);
            std::uniform_int_distribution<int> roll(0, 99);
            if (roll(rng) < 30) {
                settlement_.enemies_captured++;
                settlement_.captured_officer_ids.push_back(u.officer_id);
            }
        }
    }
}

void BattleStateData::apply_status_effect(BattleUnit& unit, StatusEffect::Type type,
                                           int32_t turns, int32_t damage_per_turn) {
    // Remove existing effect of same type
    unit.status_effects.erase(
        std::remove_if(unit.status_effects.begin(), unit.status_effects.end(),
                       [type](const StatusEffect& se) { return se.type == type; }),
        unit.status_effects.end());

    // Add new effect
    StatusEffect se;
    se.type = type;
    se.turns_remaining = turns;
    se.damage_per_turn = damage_per_turn;
    unit.status_effects.push_back(se);

    add_event(fmt::format("{} afflicted with {}!", unit.name,
                          type == StatusEffect::Type::Silence ? "Silence" :
                          type == StatusEffect::Type::Poison ? "Poison" :
                          type == StatusEffect::Type::Counterattack ? "Counterattack" :
                          type == StatusEffect::Type::Sleep ? "Sleep" :
                          type == StatusEffect::Type::Slow ? "Slow" :
                          type == StatusEffect::Type::Weakness ? "Weakness" :
                          type == StatusEffect::Type::ArmorBreak ? "Armor Break" :
                          type == StatusEffect::Type::Burn ? "Burn" :
                          "effect"),
              false, 0, false);
}

void BattleStateData::process_status_effects(BattleUnit& unit) {
    // Process Poison damage
    if (unit.has_status(StatusEffect::Type::Poison)) {
        int32_t poison_dmg = 15;  // Fixed poison damage per turn
        unit.hp = std::max(0, unit.hp - poison_dmg);
        add_event(fmt::format("{} takes {} poison damage! HP: {}",
                              unit.name, poison_dmg, unit.hp),
                  unit.is_player, poison_dmg, false);
        if (unit.is_dead()) {
            terrain_grid_[idx(unit.grid_x, unit.grid_y)].unit_id = 0;
            add_event(fmt::format("{} succumbed to poison!", unit.name),
                      unit.is_player);
            check_win_conditions();
        }
    }

    // Process Burn damage
    if (unit.has_status(StatusEffect::Type::Burn)) {
        int32_t burn_dmg = 10;  // Fixed burn damage per turn
        unit.hp = std::max(0, unit.hp - burn_dmg);
        add_event(fmt::format("{} takes {} burn damage! HP: {}",
                              unit.name, burn_dmg, unit.hp),
                  unit.is_player, burn_dmg, false);
        if (unit.is_dead()) {
            terrain_grid_[idx(unit.grid_x, unit.grid_y)].unit_id = 0;
            add_event(fmt::format("{} was consumed by fire!", unit.name),
                      unit.is_player);
            check_win_conditions();
        }
    }

    // Sleeping units skip their turn (has_acted stays true from previous turn)
    if (unit.has_status(StatusEffect::Type::Sleep)) {
        add_event(fmt::format("{} is asleep and cannot act!", unit.name),
                  unit.is_player, 0, false);
    }

    // Decrement turn counters and remove expired effects
    auto it = unit.status_effects.begin();
    while (it != unit.status_effects.end()) {
        it->turns_remaining--;
        if (it->turns_remaining <= 0) {
            std::string effect_name;
            switch (it->type) {
                case StatusEffect::Type::Silence:   effect_name = "Silence"; break;
                case StatusEffect::Type::Poison:    effect_name = "Poison"; break;
                case StatusEffect::Type::Counterattack: effect_name = "Counterattack"; break;
                case StatusEffect::Type::Sleep:     effect_name = "Sleep"; break;
                case StatusEffect::Type::Slow:      effect_name = "Slow"; break;
                case StatusEffect::Type::Weakness:  effect_name = "Weakness"; break;
                case StatusEffect::Type::ArmorBreak: effect_name = "Armor Break"; break;
                case StatusEffect::Type::Burn:      effect_name = "Burn"; break;
                default: effect_name = "effect"; break;
            }
            add_event(fmt::format("{}'s {} wore off.", unit.name, effect_name),
                      unit.is_player, 0, false);
            it = unit.status_effects.erase(it);
        } else {
            ++it;
        }
    }
}

void BattleStateData::change_weather() {
    std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<int> dist(0, 3);
    weather_ = dist(rng);
    weather_turns_ = 2 + (rng() % 3);  // Weather lasts 2-4 turns

    const char* weather_names[] = {"Clear", "Rain", "Snow", "Fog"};
    LOG_INFO("BattleStateData: weather changed to {} for {} turns",
             weather_names[weather_], weather_turns_);

    add_event(fmt::format("Weather changed to {}!", weather_names[weather_]),
              true, 0, false);
}

void BattleStateData::set_weather(int32_t w) {
    weather_ = w;
    weather_turns_ = 1;
    const char* weather_names[] = {"Clear", "Rain", "Snow", "Fog"};
    LOG_INFO("BattleStateData: weather set to {}", weather_names[w]);
}

int32_t BattleStateData::weather_movement_penalty() const {
    switch (weather_) {
        case 1:  // Rain - mud slows movement
            return 1;
        case 2:  // Snow - deep snow slows movement significantly
            return 2;
        case 3:  // Fog - reduced visibility but movement unaffected
            return 0;
        default:  // Clear
            return 0;
    }
}

int32_t BattleStateData::weather_attack_penalty() const {
    switch (weather_) {
        case 1:  // Rain - ranged attacks less effective
            return -1;
        case 2:  // Snow - visibility and movement reduced
            return -1;
        case 3:  // Fog - hard to see targets
            return -2;
        default:  // Clear
            return 0;
    }
}

bool BattleStateData::weather_blocks_vision() const {
    return weather_ == 3;  // Fog blocks vision
}

// Routing system - check if unit should rout due to low morale
bool BattleStateData::unit_rout_check(BattleUnit& unit) {
    // Units with very low morale (below 20) have a chance to rout
    if (unit.morale >= 20) return false;

    // Lower morale = higher rout chance
    int32_t rout_chance = (20 - unit.morale) * 2;  // Up to 40% at morale 0

    std::mt19937 rng(unit.id * 1000 + unit.morale);
    std::uniform_int_distribution<int> dist(0, 100);

    if (dist(rng) < rout_chance) {
        return true;  // Unit routs
    }
    return false;
}

void BattleStateData::process_unit_rout(BattleUnit& unit) {
    unit.is_routed = true;
    unit.rout_turns_remaining = 3;  // Routed for 3 turns
    unit.move_left = 0;  // Cannot act this turn
    unit.has_acted = true;

    add_event(fmt::format("{} ROUTED! Fleeing the battlefield!", unit.name),
              unit.is_player, 0, false);

    LOG_INFO("BattleStateData: unit {} routed due to low morale", unit.name);
}

bool BattleStateData::is_unit_routed(uint32_t unit_id) const {
    for (const auto& u : units_) {
        if (u.id == unit_id) return u.is_routed;
    }
    return false;
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
            // Morale slowly recovers at turn start
            u.morale = std::min(u.max_morale, u.morale + 5);
            // Process status effects at start of unit's turn
            process_status_effects(u);
            // Sleeping units cannot act this turn
            if (u.is_sleeping()) {
                u.has_acted = true;
            }
            // Check for rout due to low morale
            if (unit_rout_check(u)) {
                process_unit_rout(u);
            }
        }
    }

    // Weather changes every few turns
    if (weather_turns_ > 0) {
        weather_turns_--;
        if (weather_turns_ == 0) {
            change_weather();
        }
    } else {
        // 30% chance of weather change each turn
        std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
        std::uniform_int_distribution<int> dist(0, 100);
        if (dist(rng) < 30) {
            change_weather();
        }
    }

    check_win_conditions();

    // If it's enemy turn, execute AI automatically
    if (!is_player_turn_ && result_ == BattleResult::Ongoing) {
        ai_turn();
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

bool BattleStateData::grant_exp(BattleUnit& unit, int32_t exp_amount) {
    unit.exp += exp_amount;
    settlement_.total_exp_gained += exp_amount;

    bool leveled_up = false;
    while (unit.exp >= unit.exp_to_next) {
        level_up(unit);
        leveled_up = true;
    }
    return leveled_up;
}

void BattleStateData::level_up(BattleUnit& unit) {
    unit.exp -= unit.exp_to_next;
    unit.level++;
    unit.exp_to_next = unit.level * 100;  // Increasing requirement
    unit.max_hp += 20;
    unit.hp = unit.max_hp;  // Full heal on level up
    unit.attack += 3;
    unit.defense += 2;
    unit.move += (unit.level % 3 == 0) ? 1 : 0;
    unit.max_morale += 10;
    unit.morale = unit.max_morale;

    add_event(fmt::format("{} leveled up to Lv.{}! HP/ATK/DEF increased.",
                          unit.name, unit.level), true);
}

BattleSettlement BattleStateData::get_settlement() const {
    return settlement_;
}

int32_t BattleStateData::use_skill(BattleUnit& user, const BattleSkill& skill,
                                    int32_t tx, int32_t ty) {
    if (user.has_acted || user.used_skill) return 0;
    if (user.morale < skill.morale_cost) return 0;
    // Silenced units cannot use skills
    if (user.is_silenced()) return 0;

    // Check if target is in range
    if (!is_in_skill_range(user.grid_x, user.grid_y, tx, ty, skill)) return 0;

    user.has_acted = true;
    user.used_skill = true;
    user.move_left = 0;
    user.morale = std::max(0, user.morale - skill.morale_cost);

    int32_t dmg = 0;

    switch (skill.type) {
        case SkillType::Heal: {
            // Heal self or adjacent ally
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player == user.is_player) {
                int32_t heal = user.effective_attack() * 3 / 2;
                target->hp = std::min(target->max_hp, target->hp + heal);
                add_event(fmt::format("{} heals {} for {} HP!", user.name,
                                      target->name, heal),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::MoraleBoost: {
            // Restore morale to self or adjacent ally
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player == user.is_player) {
                target->morale = std::min(target->max_morale, target->morale + 30);
                add_event(fmt::format("{} boosts {}'s morale!", user.name,
                                      target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::PowerStrike: {
            // Strong melee attack
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                int32_t dist = std::abs(user.grid_x - tx) +
                               std::abs(user.grid_y - ty);
                if (dist <= 1) {
                    int32_t eff_atk = user.effective_attack() + 10;
                    int32_t eff_def = target->effective_defense() +
                                      defense_bonus(target->grid_x, target->grid_y);
                    dmg = roll_damage(eff_atk, eff_def);
                    target->hp = std::max(0, target->hp - dmg);
                    add_event(fmt::format("{} uses Power Strike on {} for {} dmg!",
                                          user.name, target->name, dmg),
                              user.is_player, dmg, true);
                    if (target->is_dead()) {
                        terrain_grid_[idx(target->grid_x, target->grid_y)].unit_id = 0;
                        add_event(fmt::format("{} defeated!", target->name),
                                  user.is_player);
                        if (user.is_player) {
                            bool leveled = grant_exp(user, 50);
                            if (leveled) {
                                add_event(fmt::format("{} leveled up to Lv.{}!",
                                                      user.name, user.level),
                                          true);
                            }
                        }
                        check_win_conditions();
                    }
                }
            }
            break;
        }
        case SkillType::Guard: {
            // Temporary defense boost - represented by morale gain
            user.morale = std::min(user.max_morale, user.morale + 20);
            add_event(fmt::format("{} takes a defensive stance!", user.name),
                      user.is_player, 0, true);
            break;
        }
        case SkillType::Inspire: {
            // Boost attack of adjacent allies
            for (auto& ally : units_) {
                if (ally.is_dead() || !ally.is_player) continue;
                int32_t dist = std::abs(user.grid_x - ally.grid_x) +
                               std::abs(user.grid_y - ally.grid_y);
                if (dist >= 1 && dist <= 1) {
                    // Temporary attack boost via morale
                    ally.morale = std::min(ally.max_morale, ally.morale + 15);
                }
            }
            add_event(fmt::format("{} inspires nearby allies!", user.name),
                      user.is_player, 0, true);
            break;
        }
        case SkillType::RainOfArrows: {
            // Archer-only: damage all enemies in range 3
            if (user.unit_type != UnitType::Archer) break;
            for (auto& target : units_) {
                if (target.is_dead() || target.is_player) continue;
                int32_t dist = std::abs(user.grid_x - target.grid_x) +
                               std::abs(user.grid_y - target.grid_y);
                if (dist <= 3) {
                    int32_t eff_atk = user.effective_attack();
                    int32_t eff_def = target.effective_defense() +
                                      defense_bonus(target.grid_x, target.grid_y);
                    int32_t skill_dmg = roll_damage(eff_atk, eff_def) / 2;
                    target.hp = std::max(0, target.hp - skill_dmg);
                    dmg += skill_dmg;
                    if (target.is_dead()) {
                        terrain_grid_[idx(target.grid_x, target.grid_y)].unit_id = 0;
                        if (user.is_player) {
                            bool leveled = grant_exp(user, 20);
                            if (leveled) {
                                add_event(fmt::format("{} leveled up to Lv.{}!",
                                                      user.name, user.level),
                                          true);
                            }
                        }
                    }
                }
            }
            add_event(fmt::format("{} rains arrows on enemies!", user.name),
                      user.is_player, dmg, true);
            check_win_conditions();
            break;
        }
        case SkillType::Silence: {
            // Silence: enemy cannot use skills for 2 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Silence, 2);
                add_event(fmt::format("{} silences {}! No skills for 2 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Poison: {
            // Poison: enemy takes damage each turn for 3 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Poison, 3, 15);
                add_event(fmt::format("{} poisons {}! Damage each turn for 3 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Counterattack: {
            // Counterattack: auto-attack when attacked for 2 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player == user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Counterattack, 2);
                add_event(fmt::format("{} readies counterattack! Will retaliate when attacked.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Sleep: {
            // Sleep: enemy cannot act for 2 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Sleep, 2);
                add_event(fmt::format("{} puts {} to sleep! Cannot act for 2 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Slow: {
            // Slow: enemy movement halved for 2 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Slow, 2);
                add_event(fmt::format("{} slows {}! Movement halved for 2 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Weakness: {
            // Weakness: enemy attack reduced for 3 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Weakness, 3);
                add_event(fmt::format("{} weakens {}! Attack reduced for 3 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::ArmorBreak: {
            // ArmorBreak: enemy defense reduced for 3 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::ArmorBreak, 3);
                add_event(fmt::format("{} breaks {}'s armor! Defense reduced for 3 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::Burn: {
            // Burn: enemy takes fire damage each turn for 3 turns
            BattleUnit* target = get_unit_at(tx, ty);
            if (target && !target->is_dead() && target->is_player != user.is_player) {
                apply_status_effect(*target, StatusEffect::Type::Burn, 3, 10);
                add_event(fmt::format("{} burns {}! Fire damage each turn for 3 turns.",
                                      user.name, target->name),
                          user.is_player, 0, true);
            }
            break;
        }
        case SkillType::HealAll: {
            // Heal all adjacent allies
            int32_t heal_count = 0;
            for (auto& ally : units_) {
                if (ally.is_dead() || !ally.is_player) continue;
                int32_t dist = std::abs(user.grid_x - ally.grid_x) +
                               std::abs(user.grid_y - ally.grid_y);
                if (dist >= 1 && dist <= skill.range) {
                    int32_t heal = user.effective_attack() * 2;
                    ally.hp = std::min(ally.max_hp, ally.hp + heal);
                    heal_count++;
                }
            }
            add_event(fmt::format("{} heals all allies for {} HP!", user.name,
                                  user.effective_attack() * 2),
                      user.is_player, 0, true);
            break;
        }
        case SkillType::InspireAll: {
            // Boost morale of all allies
            int32_t inspired_count = 0;
            for (auto& ally : units_) {
                if (ally.is_dead() || !ally.is_player) continue;
                int32_t dist = std::abs(user.grid_x - ally.grid_x) +
                               std::abs(user.grid_y - ally.grid_y);
                if (dist >= 1 && dist <= skill.range) {
                    ally.morale = std::min(ally.max_morale, ally.morale + 25);
                    inspired_count++;
                }
            }
            add_event(fmt::format("{} inspires all allies! Morale boosted.",
                                  user.name),
                      user.is_player, 0, true);
            break;
        }
        case SkillType::TacticalRetreat: {
            // Move and gain defensive bonus (morale)
            user.morale = std::min(user.max_morale, user.morale + 25);
            user.has_acted = true;
            user.used_skill = true;
            user.move_left = 0;
            user.morale = std::max(0, user.morale - skill.morale_cost);
            add_event(fmt::format("{} takes a tactical retreat! Morale boosted.",
                                  user.name),
                      user.is_player, 0, true);
            break;
        }
        default:
            break;
    }

    return dmg;
}

std::vector<std::pair<int32_t, int32_t>> BattleStateData::get_skill_range(
    const BattleUnit& user, const BattleSkill& skill) const {
    std::vector<std::pair<int32_t, int32_t>> cells;

    if (skill.aoe) {
        // AOE: all cells within radius
        for (int32_t y = 0; y < grid_height_; ++y) {
            for (int32_t x = 0; x < grid_width_; ++x) {
                int32_t dist = std::abs(x - user.grid_x) +
                               std::abs(y - user.grid_y);
                if (dist >= 1 && dist <= skill.aoe_radius) {
                    cells.push_back({x, y});
                }
            }
        }
    } else {
        // Single target: all cells within range
        for (int32_t y = 0; y < grid_height_; ++y) {
            for (int32_t x = 0; x < grid_width_; ++x) {
                int32_t dist = std::abs(x - user.grid_x) +
                               std::abs(y - user.grid_y);
                if (dist >= 1 && dist <= skill.range) {
                    cells.push_back({x, y});
                }
            }
        }
    }

    return cells;
}

bool BattleStateData::is_in_skill_range(int32_t ax, int32_t ay,
                                         int32_t tx, int32_t ty,
                                         const BattleSkill& skill) const {
    int32_t dist = std::abs(ax - tx) + std::abs(ay - ty);
    return dist >= 0 && dist <= skill.range;
}

// Improved AI: prioritize weak enemies, use flanking, use skills when possible
void BattleStateData::ai_turn() {
    for (auto& unit : units_) {
        if (unit.is_dead() || unit.is_player || unit.has_acted) continue;
        // Sleeping units cannot act
        if (unit.is_sleeping()) continue;

        // Find all player units
        std::vector<BattleUnit*> targets;
        for (auto& u : units_) {
            if (!u.is_dead() && u.is_player) {
                targets.push_back(&u);
            }
        }
        if (targets.empty()) continue;

        // Sort targets by HP percentage (weakest first)
        std::sort(targets.begin(), targets.end(),
                  [](BattleUnit* a, BattleUnit* b) {
                      float ahp = (float)a->hp / (float)a->max_hp;
                      float bhp = (float)b->hp / (float)b->max_hp;
                      return ahp < bhp;
                  });

        bool unit_done = false;

        // Try to use a skill if available and morale permits
        auto skills = unit.get_available_skills();
        if (!unit_done && unit.morale >= 20) {
            for (const auto& skill : skills) {
                if (skill.type == SkillType::Guard ||
                    skill.type == SkillType::Heal ||
                    skill.type == SkillType::MoraleBoost) {
                    // Self-buff skills: use if morale is low
                    if (unit.morale < 40 && skill.morale_cost <= unit.morale) {
                        use_skill(unit, skill, unit.grid_x, unit.grid_y);
                        unit_done = true;
                        break;
                    }
                }
            }
        }

        // Try ranged attack first (archers)
        if (!unit_done && unit.unit_type == UnitType::Archer) {
            for (auto* target : targets) {
                int32_t dist = std::abs(unit.grid_x - target->grid_x) +
                               std::abs(unit.grid_y - target->grid_y);
                if (dist >= 2 && dist <= 3) {
                    ranged_attack(unit, *target);
                    unit_done = true;
                    break;
                }
            }
        }

        // Try melee attack on weakest reachable target
        if (!unit_done) {
            for (auto* target : targets) {
                int32_t dist = std::abs(unit.grid_x - target->grid_x) +
                               std::abs(unit.grid_y - target->grid_y);
                if (dist <= 1) {
                    attack(unit, *target);
                    unit_done = true;
                    break;
                }
            }
        }

        // Try PowerStrike if available
        if (!unit_done) {
            for (const auto& skill : skills) {
                if (skill.type == SkillType::PowerStrike &&
                    skill.morale_cost <= unit.morale) {
                    for (auto* target : targets) {
                        int32_t dist = std::abs(unit.grid_x - target->grid_x) +
                                       std::abs(unit.grid_y - target->grid_y);
                        if (dist <= 1) {
                            use_skill(unit, skill, target->grid_x, target->grid_y);
                            unit_done = true;
                            break;
                        }
                    }
                }
                if (unit_done) break;
            }
        }

        // Move toward weakest target using A*
        if (!unit_done) {
            BattleUnit* weakest = targets[0];
            auto path = find_path(unit.grid_x, unit.grid_y,
                                  weakest->grid_x, weakest->grid_y,
                                  unit.move_left, &unit);
            if (!path.empty()) {
                auto [nx, ny] = path[0];
                auto& old_cell = get_terrain(unit.grid_x, unit.grid_y);
                old_cell.unit_id = 0;
                unit.grid_x = nx;
                unit.grid_y = ny;
                auto& new_cell = get_terrain(unit.grid_x, unit.grid_y);
                new_cell.unit_id = unit.id;

                int32_t cost = movement_cost(nx, ny, &unit);
                unit.move_left -= cost;

                // Check if now in attack range
                for (auto* target : targets) {
                    int32_t dist = std::abs(unit.grid_x - target->grid_x) +
                                   std::abs(unit.grid_y - target->grid_y);
                    if (dist <= 1) {
                        attack(unit, *target);
                        break;
                    }
                    // Archer ranged attack after moving
                    if (unit.unit_type == UnitType::Archer &&
                        dist >= 2 && dist <= 3) {
                        ranged_attack(unit, *target);
                        break;
                    }
                }
            }
        }
    }
}

std::vector<std::pair<int32_t, int32_t>> BattleStateData::find_path(
    int32_t sx, int32_t sy, int32_t gx, int32_t gy,
    int32_t max_cost, const BattleUnit* unit) const {
    // Simple A* pathfinding
    struct Node {
        int32_t x, y;
        int32_t g, h, f;
    };

    auto heuristic = [&](int32_t x, int32_t y) {
        return std::abs(x - gx) + std::abs(y - gy);
    };

    std::vector<std::vector<int32_t>> g_score(
        grid_height_, std::vector<int32_t>(grid_width_, 99999));
    std::vector<std::vector<bool>> closed(grid_height_,
                                          std::vector<bool>(grid_width_, false));
    // Track parent for path reconstruction
    std::vector<std::vector<std::pair<int32_t, int32_t>>> parent(
        grid_height_, std::vector<std::pair<int32_t, int32_t>>(grid_width_,
                                                               {-1, -1}));

    auto cmp = [](const Node& a, const Node& b) { return a.f > b.f; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> open(cmp);

    g_score[sy][sx] = 0;
    parent[sy][sx] = {sx, sy};
    open.push({sx, sy, 0, heuristic(sx, sy), heuristic(sx, sy)});

    while (!open.empty()) {
        auto curr = open.top();
        open.pop();

        if (closed[curr.y][curr.x]) continue;
        closed[curr.y][curr.x] = true;

        if (curr.x == gx && curr.y == gy) {
            // Reconstruct path using parent pointers
            std::vector<std::pair<int32_t, int32_t>> path;
            int32_t cx = curr.x, cy = curr.y;
            while (cx != sx || cy != sy) {
                path.push_back({cx, cy});
                auto [px, py] = parent[cy][cx];
                cx = px;
                cy = py;
            }
            std::reverse(path.begin(), path.end());
            // path[0] is first step from start, exclude start itself
            return path;
        }

        // Explore neighbors
        const int dx[4] = {0, 1, 0, -1};
        const int dy[4] = {-1, 0, 1, 0};
        for (int i = 0; i < 4; ++i) {
            int32_t nx = curr.x + dx[i];
            int32_t ny = curr.y + dy[i];
            if (!is_valid_position(nx, ny)) continue;
            if (!is_position_empty(nx, ny)) continue;  // Can't move into occupied cell
            int32_t cost = movement_cost(nx, ny, unit);
            if (curr.g + cost > max_cost) continue;

            int32_t new_g = curr.g + cost;
            if (new_g < g_score[ny][nx]) {
                g_score[ny][nx] = new_g;
                parent[ny][nx] = {curr.x, curr.y};
                int32_t f = new_g + heuristic(nx, ny);
                open.push({nx, ny, new_g, heuristic(nx, ny), f});
            }
        }
    }

    return {};  // No path found
}

// Helper to get mutable unit reference by ID
BattleUnit* BattleStateData::get_unit_by_id(uint32_t id) {
    for (auto& u : units_) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

} // namespace tensyoki

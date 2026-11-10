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

int32_t BattleStateData::attack(BattleUnit& attacker, BattleUnit& defender) {
    if (attacker.has_acted) return 0;
    if (defender.is_dead()) return 0;

    // Melee attack: must be adjacent
    int32_t dist = std::abs(attacker.grid_x - defender.grid_x) +
                   std::abs(attacker.grid_y - defender.grid_y);
    if (dist > 1) return 0;

    int32_t eff_atk = attacker.effective_attack();
    int32_t eff_def = defender.effective_defense() +
                      defense_bonus(defender.grid_x, defender.grid_y);
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
                      defense_bonus(defender.grid_x, defender.grid_y);
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
    return dist >= 1 && dist <= skill.range;
}

// Improved AI: prioritize weak enemies, use flanking, use skills when possible
void BattleStateData::ai_turn() {
    for (auto& unit : units_) {
        if (unit.is_dead() || unit.is_player || unit.has_acted) continue;

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
                                  unit.move_left);
            if (!path.empty()) {
                auto [nx, ny] = path[0];
                auto& old_cell = get_terrain(unit.grid_x, unit.grid_y);
                old_cell.unit_id = 0;
                unit.grid_x = nx;
                unit.grid_y = ny;
                auto& new_cell = get_terrain(unit.grid_x, unit.grid_y);
                new_cell.unit_id = unit.id;

                int32_t cost = movement_cost(nx, ny);
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
    int32_t max_cost) const {
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
            int32_t cost = movement_cost(nx, ny);
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
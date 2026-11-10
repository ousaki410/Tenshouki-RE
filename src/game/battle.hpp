/**
 * battle.hpp - Tactical battle system for Phase 4
 *
 * Turn-based tactical combat on a grid map with units, terrain, and combat
 * resolution.
 */

#pragma once

#include "game_context.hpp"
#include "game_state.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tensyoki {

/// Terrain type affects movement cost and defense bonus
enum class TerrainType {
    Plain,    // Movement cost 1, defense bonus 0
    Forest,   // Movement cost 2, defense bonus +1
    Mountain, // Movement cost 3, defense bonus +2
    Water,    // Movement cost 4 (impassable for most), defense bonus -1
    Castle,   // Movement cost 1, defense bonus +3 (defender only)
};

/// Unit type determines movement, attack range, and combat role
enum class UnitType {
    Infantry,  // Balanced: move 3, range 1, solid defense
    Cavalry,   // Fast: move 5, range 1, lower defense
    Archer,    // Ranged: move 2, range 2-3, low defense
};

/// Combat action type
enum class ActionType {
    None,
    Attack,   // Melee attack (range 1)
    RangedAttack,  // Ranged attack (range 2-3)
    Skill,    // Special skill
    Wait,     // Pass turn without acting
};

/// Skill type determines the effect
enum class SkillType {
    None,
    Heal,           // Restore HP to self or adjacent ally
    MoraleBoost,    // Restore morale to self or adjacent ally
    PowerStrike,    // Strong melee attack (high damage, high morale cost)
    Guard,          // Temporary defense boost for self
    Inspire,        // Boost attack of adjacent allies
    RainOfArrows,   // Archer-only: damage all enemies in range 3
    Silence,        // Enemy cannot use skills for 2 turns
    Poison,         // Enemy takes damage each turn for 3 turns
    Counterattack,  // Self: auto-counterattack when attacked for 2 turns
};

/// Status effect applied to a unit
struct StatusEffect {
    enum class Type {
        None,
        Silence,    // Cannot use skills
        Poison,     // Take damage each turn
        Counterattack, // Auto-counterattack when attacked
    };
    Type type = Type::None;
    int32_t turns_remaining = 0;  // How many turns the effect lasts
    int32_t damage_per_turn = 0;  // For Poison: damage each turn
};

/// Skill definition
struct BattleSkill {
    SkillType type = SkillType::None;
    std::string name;
    std::string description;
    int32_t morale_cost = 0;   // Morale required to use
    int32_t range = 1;         // Skill range in tiles
    bool target_enemy = false; // Whether skill targets enemies
    bool aoe = false;          // Area of effect
    int32_t aoe_radius = 1;    // AOE radius if applicable
};

/// Combat unit (a squadron led by an officer)
struct BattleUnit {
    uint32_t id = 0;
    uint32_t officer_id = 0;
    std::string name;
    bool is_player = true;
    UnitType unit_type = UnitType::Infantry;

    // Combat stats
    int32_t hp = 100;
    int32_t max_hp = 100;
    int32_t attack = 20;
    int32_t defense = 10;
    int32_t move = 3;        // Movement range in tiles
    int32_t move_left = 3;   // Movement remaining this turn
    int32_t attack_range = 1; // Attack range in tiles
    bool has_acted = false;  // Has acted this turn
    bool used_skill = false; // Has used skill this turn

    // Morale system (0-100, affects combat performance)
    int32_t morale = 50;
    int32_t max_morale = 100;

    // Experience
    int32_t exp = 0;
    int32_t exp_to_next = 100;
    int32_t level = 1;

    // Position on grid
    int32_t grid_x = 0;
    int32_t grid_y = 0;

    // Status effects currently applied to this unit
    std::vector<StatusEffect> status_effects;

    bool is_dead() const { return hp <= 0; }

    // Check if unit has a specific status effect
    bool has_status(StatusEffect::Type type) const {
        for (const auto& se : status_effects) {
            if (se.type == type) return true;
        }
        return false;
    }

    // Check if unit has Counterattack status
    bool has_counterattack() const {
        return has_status(StatusEffect::Type::Counterattack);
    }

    // Check if unit is silenced (cannot use skills)
    bool is_silenced() const {
        return has_status(StatusEffect::Type::Silence);
    }

    // Get effective attack with morale bonus
    int32_t effective_attack() const {
        return attack + (morale - 50) / 10;
    }

    // Get effective defense with morale bonus
    int32_t effective_defense() const {
        return defense + (morale - 50) / 10;
    }

    // Get attack range based on unit type
    int32_t get_attack_range() const {
        switch (unit_type) {
            case UnitType::Archer:    return 3;
            case UnitType::Cavalry:   return 1;
            case UnitType::Infantry:  return 1;
        }
        return 1;
    }

    // Get available skills based on unit type and level
    std::vector<BattleSkill> get_available_skills() const;
};

/// Single terrain cell on the battlefield
struct TerrainCell {
    TerrainType type = TerrainType::Plain;
    uint32_t unit_id = 0;  // Unit occupying this cell, or 0 if empty
};

/// Battle result
enum class BattleResult {
    Ongoing,
    PlayerVictory,
    PlayerDefeat,
    Draw,
};

/// Battle event for logging
struct BattleEvent {
    std::string message;
    bool is_player_action = true;
    int32_t damage = 0;
    bool is_skill = false;
};

/// Battle result summary for post-battle settlement
struct BattleSettlement {
    BattleResult result = BattleResult::Ongoing;
    int32_t total_exp_gained = 0;
    int32_t total_gold_gained = 0;
    int32_t enemies_defeated = 0;
    int32_t enemies_captured = 0;
    int32_t player_units_lost = 0;
    std::vector<std::string> level_ups;  // Unit names that leveled up
    std::vector<uint32_t> captured_officer_ids;  // IDs of captured enemy officers
};

/// Tactical battle state machine
class BattleStateData {
public:
    BattleStateData();
    ~BattleStateData() = default;

    // Initialize a test battle
    void init_battle();

    // Initialize battle from castle data (map-to-battle linkage)
    void init_from_castle(uint32_t castle_index, bool is_attacker);

    // Get unit at grid position (or null)
    BattleUnit* get_unit_at(int32_t x, int32_t y);
    const BattleUnit* get_unit_at(int32_t x, int32_t y) const;

    // Get terrain at grid position
    TerrainCell& get_terrain(int32_t x, int32_t y);
    const TerrainCell& get_terrain(int32_t x, int32_t y) const;

    // Check if position is valid and empty
    bool is_valid_position(int32_t x, int32_t y) const;
    bool is_position_empty(int32_t x, int32_t y) const;

    // Get movement cost for entering a cell
    int32_t movement_cost(int32_t x, int32_t y) const;

    // Get defense bonus for a unit at a position
    int32_t defense_bonus(int32_t x, int32_t y) const;

    // Execute an attack from attacker to defender, returns damage dealt
    int32_t attack(BattleUnit& attacker, BattleUnit& defender);

    // Execute a ranged attack (for archers), returns damage dealt
    int32_t ranged_attack(BattleUnit& attacker, BattleUnit& defender);

    // Check if position is within attack range (depends on unit type)
    bool is_in_attack_range(int32_t ax, int32_t ay,
                            int32_t tx, int32_t ty,
                            const BattleUnit& attacker) const;

    // Check if position is within ranged attack range (2-3 tiles)
    bool is_in_ranged_range(int32_t ax, int32_t ay,
                            int32_t tx, int32_t ty) const;

    // Execute a skill from attacker to target(s), returns damage dealt or 0 for non-damage skills
    int32_t use_skill(BattleUnit& user, const BattleSkill& skill,
                      int32_t tx, int32_t ty);

    // Get skill range highlight cells
    std::vector<std::pair<int32_t, int32_t>> get_skill_range(
        const BattleUnit& user, const BattleSkill& skill) const;

    // Check if position is within skill range
    bool is_in_skill_range(int32_t ax, int32_t ay, int32_t tx, int32_t ty,
                           const BattleSkill& skill) const;

    // Get battle settlement (called after battle ends)
    BattleSettlement get_settlement() const;

    // Getters
    int32_t grid_width() const { return grid_width_; }
    int32_t grid_height() const { return grid_height_; }
    BattleResult result() const { return result_; }
    bool is_player_turn() const { return is_player_turn_; }
    uint32_t selected_unit_id() const { return selected_unit_id_; }
    void set_selected_unit_id(uint32_t id) { selected_unit_id_ = id; }
    int32_t selected_action() const { return selected_action_; }
    void set_selected_action(int32_t action) { selected_action_ = action; }

    // Get all units (read-only)
    const std::vector<BattleUnit>& units() const { return units_; }
    // Get mutable units
    std::vector<BattleUnit>& units() { return units_; }

    // Get battle log
    const std::vector<BattleEvent>& events() const { return events_; }

    // Turn management
    void next_turn();
    bool all_player_units_acted() const;
    bool all_enemy_units_acted() const;

    // AI: Execute AI turn for all enemy units
    void ai_turn();

    // Pathfinding: A* from start to goal, returns path (excluding start)
    std::vector<std::pair<int32_t, int32_t>> find_path(int32_t sx, int32_t sy,
                                                       int32_t gx, int32_t gy,
                                                       int32_t max_cost) const;

    // Check if a unit can reach a target position within remaining movement
    bool can_reach(int32_t sx, int32_t sy, int32_t tx, int32_t ty,
                   int32_t move_left) const;

private:
    int32_t grid_width_ = 20;
    int32_t grid_height_ = 15;
    std::vector<TerrainCell> terrain_grid_;
    std::vector<BattleUnit> units_;
    BattleResult result_ = BattleResult::Ongoing;
    bool is_player_turn_ = true;
    uint32_t selected_unit_id_ = 0;
    int32_t selected_action_ = 0;  // 0=Attack, 1=Ranged, 2=Skill, 3=Wait
    uint32_t next_unit_id_ = 1;
    std::vector<BattleEvent> events_;
    BattleSettlement settlement_;

    // Helper: index into terrain grid
    int32_t idx(int32_t x, int32_t y) const { return y * grid_width_ + x; }

    // Roll damage dice
    int32_t roll_damage(int32_t attack, int32_t defense) const;

    // Add a battle event to the log
    void add_event(const std::string& message, bool is_player_action,
                   int32_t damage = 0, bool is_skill = false);

    // Check win conditions and build settlement
    void check_win_conditions();

    // Build settlement data after battle ends
    void build_settlement();

    // Grant experience to a unit, returns true if leveled up
    bool grant_exp(BattleUnit& unit, int32_t exp_amount);

    // Level up a unit
    void level_up(BattleUnit& unit);

    // Apply a status effect to a unit
    void apply_status_effect(BattleUnit& unit, StatusEffect::Type type,
                             int32_t turns, int32_t damage_per_turn = 0);

    // Process all status effects at turn start (poison damage, expiry)
    void process_status_effects(BattleUnit& unit);

    // Check if unit can counterattack when attacked
    bool can_counterattack(const BattleUnit& unit) const;

    // AI helper: get mutable unit by ID
    BattleUnit* get_unit_by_id(uint32_t id);
};

} // namespace tensyoki

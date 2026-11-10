/**
 * game_states.hpp - Concrete game state implementations for Phase 4
 */

#pragma once

#include "battle.hpp"
#include "castle_command.hpp"
#include "game_context.hpp"
#include "game_state.hpp"
#include "map_view.hpp"
#include <memory>

namespace tensyoki {

/// Main menu state
class MainMenuState : public GameState {
public:
    explicit MainMenuState(GameContext& ctx);
    void enter() override;
    bool update(float dt) override;
    void render() override;
    GameStateID id() const override { return GameStateID::MainMenu; }

private:
    GameContext& ctx_;
    int selected_item_ = 0;  // 0=New Game, 1=Load, 2=Quit
    bool show_load_menu_ = false;
    int selected_save_slot_ = 1;

    void render_load_menu();
};

/// Map view state: strategic layer
class MapViewState : public GameState {
public:
    explicit MapViewState(GameContext& ctx);
    void enter() override;
    bool update(float dt) override;
    void render() override;
    GameStateID id() const override { return GameStateID::MapView; }

private:
    GameContext& ctx_;
    MapView map_view_;
    bool init_done_ = false;
    int selected_officer_ = -1;  // Selected officer from garrison list
    int selected_castle_ = -1;   // Selected castle index for commands
    int last_month_ = -1;        // Track month for resource production
};

/// Officer detail state
class OfficerDetailState : public GameState {
public:
    explicit OfficerDetailState(GameContext& ctx);
    void enter() override;
    bool update(float dt) override;
    void render() override;
    GameStateID id() const override { return GameStateID::OfficerDetail; }

private:
    GameContext& ctx_;
    int selected_officer_ = -1;
};

/// Castle command state: manage castle facilities, troops, officers
class CastleCommandState : public GameState {
public:
    explicit CastleCommandState(GameContext& ctx);
    void enter() override;
    bool update(float dt) override;
    void render() override;
    GameStateID id() const override { return GameStateID::CastleCommand; }

private:
    GameContext& ctx_;
    int selected_castle_ = -1;
    int selected_tab_ = 0;  // 0=Recruit, 1=Build, 2=Officer, 3=Upgrade
};

/// Battle state: tactical combat layer
class BattleState : public GameState {
public:
    explicit BattleState(GameContext& ctx);
    void enter() override;
    bool update(float dt) override;
    void render() override;
    GameStateID id() const override { return GameStateID::Battle; }

private:
    GameContext& ctx_;
    BattleStateData battle_data_;
    bool init_done_ = false;
    int selected_unit_id_ = -1;   // Currently selected unit ID
    int selected_action_ = 0;     // 0=Melee, 1=Ranged, 2=Skill, 3=Wait
    int selected_skill_idx_ = 0;  // Index into available skills
    bool show_move_range_ = false; // Show movement range highlight
    bool show_skill_range_ = false; // Show skill range highlight
    // Cells reachable by selected unit (for movement range highlight)
    std::vector<std::pair<int32_t, int32_t>> movable_cells_;
    // Cells with enemies in melee attack range (for attack highlight)
    std::vector<std::pair<int32_t, int32_t>> attackable_cells_;
    // Cells with enemies in ranged attack range (for ranged highlight)
    std::vector<std::pair<int32_t, int32_t>> ranged_attackable_cells_;
    // Cells in skill range (for skill highlight)
    std::vector<std::pair<int32_t, int32_t>> skill_range_cells_;
    // Show battle settlement dialog
    bool show_settlement_ = false;
};

} // namespace tensyoki

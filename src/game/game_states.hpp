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
    int selected_command_ = 0;  // 0=View Info, 1=Recruit Troops, 2=Build Wall, 3=Build Barracks, 4=Back
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
    bool show_move_range_ = false; // Show movement range highlight
    // Cells reachable by selected unit (for movement range highlight)
    std::vector<std::pair<int32_t, int32_t>> movable_cells_;
    // Cells with enemies in attack range (for attack highlight)
    std::vector<std::pair<int32_t, int32_t>> attackable_cells_;
    // Battle log messages
    std::vector<std::string> battle_log_;
};

} // namespace tensyoki

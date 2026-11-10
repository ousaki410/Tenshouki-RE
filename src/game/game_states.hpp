/**
 * game_states.hpp - Concrete game state implementations for Phase 4
 */

#pragma once

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

} // namespace tensyoki

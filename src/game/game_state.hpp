/**
 * game_state.hpp - Game state machine for Phase 4
 *
 * Provides a stack-based state machine to manage game screens:
 *   - MainMenu:   Start/new game/load/quit
 *   - MapView:    Strategic map with castles, officers, routes
 *   - OfficerDetail: Officer information and commands
 *   - CastleDetail: Castle information, garrison, facilities
 *   - Battle:     Tactical battle (placeholder)
 *
 * States are pushed/popped; the top state receives update/render calls.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <functional>

namespace tensyoki {

/// Game state identifiers
enum class GameStateID {
    MainMenu,
    MapView,
    OfficerDetail,
    CastleDetail,
    CastleCommand,
    Battle,
    Quit,
};

/// Abstract game state
class GameState {
public:
    GameState() = default;
    virtual ~GameState() = default;

    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    /// Called when the state becomes active
    virtual void enter() {}

    /// Called when the state is about to be deactivated
    virtual void leave() {}

    /// Update logic (returns true to continue, false to request pop)
    virtual bool update(float dt) { return true; }

    /// Render the state
    virtual void render() {}

    /// Get the state ID
    virtual GameStateID id() const = 0;

    /// Whether this state blocks input to states below it
    virtual bool blocks_input() const { return true; }

    /// Whether this state blocks rendering of states below it
    virtual bool blocks_render() const { return true; }
};

/// Stack-based state manager
class GameStateManager {
public:
    GameStateManager() = default;
    ~GameStateManager() = default;

    /// Push a new state onto the stack (becomes active)
    void push(std::unique_ptr<GameState> state);

    /// Pop the current state
    void pop();

    /// Replace the current state with a new one
    void replace(std::unique_ptr<GameState> state);

    /// Request a quit (sets quit flag)
    void request_quit();

    /// Update the top state
    bool update(float dt);

    /// Render all active states (from bottom to top, skipping blocked ones)
    void render();

    /// Whether the game should quit
    bool should_quit() const { return quit_requested_; }

    /// Get the current top state ID
    GameStateID current_id() const;

    /// Clear all states
    void clear();

private:
    std::stack<std::unique_ptr<GameState>> states_;
    std::vector<std::unique_ptr<GameState>> pending_push_;
    bool pending_pop_ = false;
    std::unique_ptr<GameState> pending_replace_;
    bool quit_requested_ = false;

    void apply_pending();
};

} // namespace tensyoki

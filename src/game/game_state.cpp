/**
 * game_state.cpp - Game state machine implementation
 */

#include "game_state.hpp"
#include "core/logging.hpp"

namespace tensyoki {

void GameStateManager::push(std::unique_ptr<GameState> state) {
    GameStateID id = state->id();
    LOG_DEBUG("GameStateManager: push state {}", static_cast<int>(id));
    pending_push_.push_back(std::move(state));
}

void GameStateManager::pop() {
    LOG_DEBUG("GameStateManager: pop state");
    pending_pop_ = true;
}

void GameStateManager::replace(std::unique_ptr<GameState> state) {
    GameStateID id = state->id();
    LOG_DEBUG("GameStateManager: replace state {}", static_cast<int>(id));
    pending_replace_ = std::move(state);
}

void GameStateManager::request_quit() {
    LOG_DEBUG("GameStateManager: quit requested");
    quit_requested_ = true;
}

void GameStateManager::apply_pending() {
    // Leave current state before replacing/popping
    if (pending_replace_ || pending_pop_) {
        if (!states_.empty()) {
            states_.top()->leave();
        }
    }

    if (pending_replace_) {
        if (!states_.empty()) {
            states_.pop();
        }
        states_.push(std::move(pending_replace_));
        pending_replace_ = nullptr;
        if (!states_.empty()) {
            states_.top()->enter();
        }
    } else if (pending_pop_) {
        if (!states_.empty()) {
            states_.pop();
        }
        if (!states_.empty()) {
            states_.top()->enter();
        }
    }

    for (auto& s : pending_push_) {
        if (!states_.empty()) {
            states_.top()->leave();  // block input/render below
        }
        states_.push(std::move(s));
        states_.top()->enter();
    }
    pending_push_.clear();
    pending_pop_ = false;
}

bool GameStateManager::update(float dt) {
    apply_pending();

    if (states_.empty()) {
        return false;
    }

    // Only the top state updates (unless it doesn't block input)
    bool top_blocks_input = states_.top()->blocks_input();

    // Find the topmost state that doesn't block input
    size_t active_idx = states_.size() - 1;
    while (active_idx > 0) {
        // We can't easily access by index with std::stack, so just update top
        break;
    }

    bool cont = states_.top()->update(dt);
    if (!cont) {
        pop();
        apply_pending();
    }
    return !states_.empty() && !quit_requested_;
}

void GameStateManager::render() {
    apply_pending();

    if (states_.empty()) {
        return;
    }

    // Collect states from bottom to top
    // std::stack doesn't support iteration, so we pop into a vector
    std::vector<std::unique_ptr<GameState>> ordered;
    ordered.reserve(states_.size());
    while (!states_.empty()) {
        ordered.push_back(std::move(states_.top()));
        states_.pop();
    }
    // Now ordered[0] is bottom, ordered.back() is top

    // Render states that don't block render, from bottom up
    for (size_t i = 0; i < ordered.size(); ++i) {
        ordered[i]->render();
        if (ordered[i]->blocks_render() && i + 1 < ordered.size()) {
            // Stop rendering lower states once a blocking state is reached
            // (but still keep the blocking state and those above it)
            break;
        }
    }

    // Restore the stack (bottom to top)
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        states_.push(std::move(*it));
    }
}

GameStateID GameStateManager::current_id() const {
    if (states_.empty()) {
        return GameStateID::MainMenu;
    }
    return states_.top()->id();
}

void GameStateManager::clear() {
    while (!states_.empty()) {
        states_.top()->leave();
        states_.pop();
    }
}

} // namespace tensyoki

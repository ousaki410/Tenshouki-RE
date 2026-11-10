/**
 * game_states.cpp - Concrete game state implementations
 */

#include "game_states.hpp"
#include "core/logging.hpp"
#include <fmt/format.h>
#include <imgui.h>

namespace tensyoki {

// ===========================================================================
// MainMenuState
// ===========================================================================

MainMenuState::MainMenuState(GameContext& ctx) : ctx_(ctx) {}

void MainMenuState::enter() {
    LOG_INFO("MainMenuState: enter");
    selected_item_ = 0;
}

bool MainMenuState::update(float dt) {
    (void)dt;
    // Input is handled via ImGui in render()
    return true;
}

void MainMenuState::render() {
    ImGui::SetNextWindowPos({400, 300}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({400, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Tensyoki - Main Menu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove);

    ImGui::Text("TENSHOUKI - RE");
    ImGui::Text("Nobunaga's Ambition: Tenshouki HD");
    ImGui::Text("Linux Port - Phase 4");
    ImGui::Separator();

    const char* items[] = {"New Game", "Load Game", "Quit"};
    for (int i = 0; i < 3; ++i) {
        bool selected = (i == selected_item_);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              selected ? ImVec4(1, 1, 0, 1) : ImVec4(1, 1, 1, 1));
        if (ImGui::Selectable(items[i], selected)) {
            selected_item_ = i;
            if (i == 0) {
                // New game -> go to map
                ctx_.date = GameDate{1, 4, 1};  // Tensho 1, April 1 (Spring)
                // This will be handled by Application
            } else if (i == 2) {
                // Quit
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::Text("Use UP/DOWN to select, ENTER to confirm");
    ImGui::End();

    // Handle keyboard input for menu navigation
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        selected_item_ = (selected_item_ - 1 + 3) % 3;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        selected_item_ = (selected_item_ + 1) % 3;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (selected_item_ == 0) {
            // Start new game
        } else if (selected_item_ == 2) {
            // Quit
        }
    }
}

// ===========================================================================
// MapViewState
// ===========================================================================

MapViewState::MapViewState(GameContext& ctx) : ctx_(ctx) {}

void MapViewState::enter() {
    LOG_INFO("MapViewState: enter");
    init_done_ = false;
}

bool MapViewState::update(float dt) {
    map_view_.update(dt);
    return true;
}

void MapViewState::render() {
    if (!init_done_ && ctx_.renderer && ctx_.sndata && ctx_.tex_cache) {
        if (map_view_.init(*ctx_.sndata, *ctx_.tex_cache, *ctx_.renderer)) {
            init_done_ = true;
            LOG_INFO("MapViewState: map initialized");
        }
    }

    if (!init_done_) {
        ImGui::Begin("Map View", nullptr, ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Loading map...");
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({(float)ctx_.renderer->get_width(),
                              (float)ctx_.renderer->get_height()},
                             ImGuiCond_Always);
    ImGui::Begin("Map", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBackground);

    // Draw map using ImGui draw list (simpler than raw OpenGL for now)
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();
    ImVec2 center = {win_pos.x + win_size.x * 0.5f, win_pos.y + win_size.y * 0.5f};

    // Camera (simple, centered for now)
    float cam_x = 0.0f, cam_y = 0.0f, zoom = 1.0f;

    // Render routes
    // We need access to routes from map_view_, but MapView doesn't expose them yet.
    // For now, render castles only.

    // Render castles
    // Note: We can't directly access castles_ from here since it's private.
    // In a full implementation, MapView would expose a render() method.
    // For now, we render castle names via ImGui.

    ImGui::End();

    // Render castle info panel
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({300, 400}, ImGuiCond_Always);
    ImGui::Begin("Map Info", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Date: %s", ctx_.date.to_string().c_str());
    ImGui::Text("Season: %s", ctx_.date.season_name().c_str());
    ImGui::Separator();
    ImGui::Text("Castles: %zu", ctx_.sndata ? ctx_.sndata->castle_count() : 0);
    ImGui::Text("Officers: %zu", ctx_.sndata ? ctx_.sndata->officer_count() : 0);
    ImGui::Separator();
    ImGui::Text("Click on a castle to select");
    ImGui::Text("ESC: Return to menu");

    if (ImGui::Button("Return to Menu")) {
        // This would be handled by Application
    }

    ImGui::End();
}

// ===========================================================================
// OfficerDetailState
// ===========================================================================

OfficerDetailState::OfficerDetailState(GameContext& ctx) : ctx_(ctx) {}

void OfficerDetailState::enter() {
    LOG_INFO("OfficerDetailState: enter");
}

bool OfficerDetailState::update(float dt) {
    (void)dt;
    return true;
}

void OfficerDetailState::render() {
    ImGui::Begin("Officer Detail", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ctx_.sndata && ctx_.sndata->officer_count() > 0) {
        ImGui::Text("Officers: %zu", ctx_.sndata->officer_count());
        ImGui::Separator();

        // Show first few officers as a list
        int count = 0;
        for (const auto& officer : ctx_.sndata->officers) {
            if (count >= 20) break;
            std::string label = fmt::format("[{}] id={} {}", officer.slot,
                                            officer.id,
                                            officer.name_utf8.empty()
                                                ? officer.name_raw
                                                : officer.name_utf8);
            if (ImGui::Selectable(label.c_str(), selected_officer_ == officer.slot)) {
                selected_officer_ = officer.slot;
            }
            ++count;
        }
    } else {
        ImGui::Text("No officer data loaded.");
    }

    ImGui::Separator();
    if (ImGui::Button("Back")) {
        // Return to map
    }

    ImGui::End();

    // Show selected officer details
    if (selected_officer_ >= 0 && ctx_.sndata) {
        for (const auto& officer : ctx_.sndata->officers) {
            if (officer.slot == static_cast<uint32_t>(selected_officer_)) {
                ImGui::Begin("Officer Details", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("Slot: %u", officer.slot);
                ImGui::Text("ID: %u", officer.id);
                ImGui::Text("Status: %u", officer.status);
                ImGui::Text("Gender: %u", officer.gender);
                ImGui::Text("Name: %s",
                            officer.name_utf8.empty()
                                ? officer.name_raw.c_str()
                                : officer.name_utf8.c_str());
                ImGui::End();
                break;
            }
        }
    }
}

} // namespace tensyoki

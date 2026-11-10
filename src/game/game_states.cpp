/**
 * game_states.cpp - Concrete game state implementations
 */

#include "game_states.hpp"
#include "core/logging.hpp"
#include <fmt/format.h>
#include <imgui.h>
#include <algorithm>

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
                ctx_.date = GameDate{1, 4, 1};
            } else if (i == 2) {
                // Quit - handled by Application
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::Text("Use UP/DOWN to select, ENTER to confirm");
    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        selected_item_ = (selected_item_ - 1 + 3) % 3;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        selected_item_ = (selected_item_ + 1) % 3;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (selected_item_ == 0) {
            ctx_.date = GameDate{1, 4, 1};
            ctx_.pending_push = GameContext::PendingState::MapView;
        } else if (selected_item_ == 2) {
            ctx_.request_pop = true;
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
    selected_officer_ = -1;
}

bool MapViewState::update(float dt) {
    map_view_.update(dt, *ctx_.renderer);
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

    map_view_.render(*ctx_.renderer);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        map_view_.handle_click(mouse.x, mouse.y, *ctx_.renderer);
    }

    ImGui::End();

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({340, 500}, ImGuiCond_Always);
    ImGui::Begin("Map Info", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Date: %s", ctx_.date.to_string().c_str());
    ImGui::Text("Season: %s", ctx_.date.season_name().c_str());
    ImGui::Separator();
    ImGui::Text("Castles: %zu", ctx_.sndata ? ctx_.sndata->castle_count() : 0);
    ImGui::Text("Officers: %zu", ctx_.sndata ? ctx_.sndata->officer_count() : 0);
    ImGui::Separator();

    auto sel = map_view_.selected_castle();
    if (sel) {
        const CastlePos* cp = nullptr;
        for (const auto& c : map_view_.castles()) {
            if (c.index == *sel) { cp = &c; break; }
        }
        if (cp) {
            ImGui::Text("Selected: %s",
                        cp->name_utf8.empty() ? cp->name.c_str()
                                              : cp->name_utf8.c_str());
            ImGui::Text("Position: (%.0f, %.0f)", cp->x, cp->y);
            ImGui::Text("Region: %u", cp->region_id);
            ImGui::Separator();

            ImGui::Text("Garrison Officers:");
            int shown = 0;
            if (ctx_.sndata) {
                for (const auto& officer : ctx_.sndata->officers) {
                    if (officer.slot % ctx_.sndata->castle_count() == cp->index) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                        if (ImGui::Selectable(
                                fmt::format("ID {}: {}", officer.id,
                                            officer.name_utf8.empty()
                                                ? officer.name_raw
                                                : officer.name_utf8)
                                        .c_str(),
                                selected_officer_ == officer.slot)) {
                            selected_officer_ = officer.slot;
                        }
                        ImGui::PopStyleColor();
                        shown++;
                    }
                }
            }
            if (shown == 0) {
                ImGui::TextDisabled("  (no officers assigned)");
            }

            ImGui::Separator();
            if (ImGui::Button("View Officer Detail")) {
                if (selected_officer_ >= 0 && ctx_.sndata) {
                    ctx_.pending_push = GameContext::PendingState::OfficerDetail;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Battle")) {
                ctx_.pending_push = GameContext::PendingState::Battle;
            }
        }
    } else {
        ImGui::Text("Click on a castle to select");
    }

    ImGui::Separator();
    ImGui::Text("Date/Time Controls:");
    ImGui::Text("Pause: %s", ctx_.clock.paused ? "ON" : "OFF");
    ImGui::SameLine();
    if (ImGui::Button(ctx_.clock.paused ? "Resume" : "Pause")) {
        ctx_.clock.paused = !ctx_.clock.paused;
    }
    ImGui::SameLine();
    ImGui::Text("Speed: %.1fx", ctx_.clock.speed);
    if (ImGui::Button("-")) {
        ctx_.clock.speed = std::max(0.5f, ctx_.clock.speed - 0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        ctx_.clock.speed = std::min(8.0f, ctx_.clock.speed + 0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("1x")) {
        ctx_.clock.speed = 1.0f;
    }
    ImGui::Separator();
    if (ImGui::Button("Return to Menu")) {
        ctx_.request_pop = true;
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

        int count = 0;
        for (const auto& officer : ctx_.sndata->officers) {
            if (count >= 30) break;
            std::string label = fmt::format("[{}] id={} {}",
                                            officer.slot,
                                            officer.id,
                                            officer.name_utf8.empty()
                                                ? officer.name_raw
                                                : officer.name_utf8);
            if (ImGui::Selectable(label.c_str(),
                                  selected_officer_ == officer.slot)) {
                selected_officer_ = officer.slot;
            }
            ++count;
        }
    } else {
        ImGui::Text("No officer data loaded.");
    }

    ImGui::Separator();
    if (ImGui::Button("Back")) {
        ctx_.request_pop = true;
    }

    ImGui::End();

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
                ImGui::Separator();
                ImGui::Text("Attributes (attr bytes):");
                ImGui::BeginChild("AttrBytes", ImVec2(260, 110), true);
                const auto& attr = officer.attr;
                for (size_t i = 0; i < attr.size(); ++i) {
                    uint8_t b = attr[i];
                    if (i == 18 || i == 19 || i == 22 || i == 23) continue;
                    ImGui::Text("  [%2u] = %3u (0x%02X)", i, b, b);
                    if ((i + 1) % 6 == 0) ImGui::Spacing();
                }
                ImGui::EndChild();
                ImGui::Separator();
                ImGui::Text("Possible attributes (hypothesis):");
                ImGui::BeginChild("AttrHypo", ImVec2(260, 80), true);
                auto show_attr = [&](size_t offset, const char* name) {
                    if (offset < attr.size()) {
                        uint8_t b = attr[offset];
                        bool valid = (b != 0xFF && b != 0);
                        ImGui::TextColored(valid ? ImVec4(0.8f, 0.9f, 1.0f, 1.0f)
                                                 : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                           "%s [%2u]: %3u", name, offset, b);
                    }
                };
                show_attr(24, "Loyalty?");
                show_attr(25, "Command?");
                show_attr(26, "Warfare?");
                show_attr(27, "Wisdom?");
                show_attr(28, "Politics?");
                show_attr(29, "Charm?");
                ImGui::EndChild();
                ImGui::End();
                break;
            }
        }
    }
}

// ===========================================================================
// BattleState
// ===========================================================================

BattleState::BattleState(GameContext& ctx) : ctx_(ctx) {}

void BattleState::enter() {
    LOG_INFO("BattleState: enter");
    init_done_ = false;
}

bool BattleState::update(float dt) {
    (void)dt;
    return true;
}

void BattleState::render() {
    ImGui::Begin("Battle", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBackground);

    ImGui::SetWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetWindowSize({(float)ctx_.renderer->get_width(),
                          (float)ctx_.renderer->get_height()},
                         ImGuiCond_Always);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    // Battlefield background
    draw->AddRectFilled(win_pos,
                        {win_pos.x + win_size.x, win_pos.y + win_size.y},
                        IM_COL32(50, 80, 120, 255));

    // Draw grid
    float grid_size = 40.0f;
    float start_x = fmodf(win_pos.x, grid_size);
    float start_y = fmodf(win_pos.y, grid_size);
    for (float x = start_x; x < win_pos.x + win_size.x; x += grid_size) {
        draw->AddLine({x, win_pos.y}, {x, win_pos.y + win_size.y},
                      IM_COL32(60, 100, 140, 60), 1.0f);
    }
    for (float y = start_y; y < win_pos.y + win_size.y; y += grid_size) {
        draw->AddLine({win_pos.x, y}, {win_pos.x + win_size.x, y},
                      IM_COL32(60, 100, 140, 60), 1.0f);
    }

    // Title
    ImGui::SetCursorPos({10, 10});
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "TACTICAL BATTLE");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "- ESC to return to map");

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Return to map - handled by Application
    }

    ImGui::End();
}

} // namespace tensyoki

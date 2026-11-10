/**
 * game_states.cpp - Concrete game state implementations
 */

#include "game_states.hpp"
#include "core/logging.hpp"
#include <fmt/format.h>
#include <imgui.h>
#include <algorithm>

namespace tensyoki {

// Helper to update terrain grid when unit moves
static void terrain_grid_cell_update(BattleStateData& bs, BattleUnit* unit,
                                     int32_t new_x, int32_t new_y);

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
                ctx_.pending_push = GameContext::PendingState::MapView;
            } else if (i == 2) {
                ctx_.request_pop = true;
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

    // Full-screen map overlay
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({(float)ctx_.renderer->get_width(),
                              (float)ctx_.renderer->get_height()},
                             ImGuiCond_Always);
    ImGui::Begin("Map", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBackground);

    map_view_.render(*ctx_.renderer);

    // Handle mouse clicks on the map
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (map_view_.handle_click(mouse.x, mouse.y, *ctx_.renderer)) {
            // Castle selected - show command options via context
            auto sel = map_view_.selected_castle();
            if (sel) {
                // Find castle index from map
                for (const auto& c : map_view_.castles()) {
                    if (c.index == *sel) {
                        // Store selected castle for command menu
                        // We'll show a small popup menu
                        ImGui::OpenPopup("CastleContext");
                    }
                }
            }
        }
    }

    // Castle context popup
    if (ImGui::BeginPopup("CastleContext")) {
        auto sel = map_view_.selected_castle();
        if (sel) {
            const CastlePos* cp = nullptr;
            for (const auto& c : map_view_.castles()) {
                if (c.index == *sel) { cp = &c; break; }
            }
            if (cp) {
                ImGui::Text("Castle: %s",
                            cp->name_utf8.empty() ? cp->name.c_str()
                                                  : cp->name_utf8.c_str());
                ImGui::Separator();
                if (ImGui::Button("View Details")) {
                    ctx_.pending_push = GameContext::PendingState::CastleCommand;
                    // Store selected castle index in context
                    // We need to pass it somehow - for now use selected_castle_
                }
                ImGui::SameLine();
                if (ImGui::Button("Battle")) {
                    ctx_.pending_push = GameContext::PendingState::Battle;
                }
                if (ImGui::Button("Close")) {
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    // Render castle info panel
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
        ctx_.selected_castle_id = *sel;
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

            // Show garrison officers (simple assignment by slot % castle_count)
            ImGui::Text("Garrison Officers:");
            int shown = 0;
            if (ctx_.sndata) {
                for (const auto& officer : ctx_.sndata->officers) {
                    if (officer.slot % ctx_.sndata->castle_count() == cp->index) {
                        ImGui::BulletText("ID %u: %s", officer.id,
                                          officer.name_utf8.empty()
                                              ? officer.name_raw.c_str()
                                              : officer.name_utf8.c_str());
                        shown++;
                    }
                }
            }
            if (shown == 0) {
                ImGui::TextDisabled("  (no officers assigned)");
            }

            ImGui::Separator();
            if (ImGui::Button("Castle Commands")) {
                ctx_.selected_castle_id = *sel;
                ctx_.pending_push = GameContext::PendingState::CastleCommand;
            }
            ImGui::SameLine();
            if (ImGui::Button("Battle")) {
                ctx_.pending_push = GameContext::PendingState::Battle;
            }
        }
    } else {
        ImGui::Text("Click on a castle to select");
    }
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
// CastleCommandState
// ===========================================================================

CastleCommandState::CastleCommandState(GameContext& ctx) : ctx_(ctx) {}

void CastleCommandState::enter() {
    LOG_INFO("CastleCommandState: enter");
    selected_castle_ = -1;
    selected_command_ = 0;
}

bool CastleCommandState::update(float dt) {
    (void)dt;
    return true;
}

void CastleCommandState::render() {
    ImGui::Begin("Castle Command", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (!ctx_.sndata || ctx_.sndata->castle_count() == 0) {
        ImGui::Text("No castle data loaded.");
        ImGui::Separator();
        if (ImGui::Button("Back")) {
            ctx_.request_pop = true;
        }
        ImGui::End();
        return;
    }

    // Castle selection
    ImGui::Text("Select Castle:");
    ImGui::BeginChild("CastleList", ImVec2(300, 200), true);
    for (const auto& castle : ctx_.castle_command.castles()) {
        bool selected = (selected_castle_ == static_cast<int>(castle.index));
        std::string label = fmt::format("[{}] {} (Lv.{})",
                                        castle.index,
                                        castle.name_utf8.empty()
                                            ? castle.name
                                            : castle.name_utf8,
                                        castle.level);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selected_castle_ = castle.index;
            selected_command_ = 0;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Command details
    if (selected_castle_ >= 0) {
        auto* castle = ctx_.castle_command.get_castle(selected_castle_);
        if (castle) {
            ImGui::BeginChild("CastleDetails", ImVec2(0, 0), true);
            ImGui::Text("Castle: %s",
                        castle->name_utf8.empty()
                            ? castle->name.c_str()
                            : castle->name_utf8.c_str());
            ImGui::Text("Level: %d", castle->level);
            ImGui::Text("Defense: %d/%d", castle->defense, castle->max_defense);
            ImGui::Text("Garrison: %d/%d", castle->garrison,
                        castle->max_garrison);
            ImGui::Text("Gold: %d", castle->gold);
            ImGui::Text("Food: %d", castle->food);
            ImGui::Separator();
            ImGui::Text("Facilities:");
            ImGui::Text("  Wall: %s", castle->has_wall ? "Yes" : "No");
            ImGui::Text("  Barracks: %s", castle->has_barracks ? "Yes" : "No");
            ImGui::Text("  Storehouse: %s", castle->has_storehouse ? "Yes" : "No");
            ImGui::Text("  Market: %s", castle->has_market ? "Yes" : "No");
            ImGui::Separator();

            // Command selection
            const char* commands[] = {"View Info", "Recruit Troops",
                                      "Build Wall", "Build Barracks",
                                      "Back"};
            for (int i = 0; i < 5; ++i) {
                bool selected = (selected_command_ == i);
                if (ImGui::Selectable(commands[i], selected)) {
                    selected_command_ = i;
                    if (i == 1) {  // Recruit
                        auto result = ctx_.castle_command.recruit_troops(
                            selected_castle_, 10);
                        if (result == CommandResult::Success) {
                            LOG_INFO("Recruited 10 troops at castle {}",
                                     selected_castle_);
                        } else if (result == CommandResult::InsufficientGold) {
                            LOG_WARN("Insufficient gold to recruit troops");
                        }
                    } else if (i == 2) {  // Build Wall
                        auto result = ctx_.castle_command.build_facility(
                            selected_castle_, FacilityType::Wall);
                        if (result == CommandResult::Success) {
                            LOG_INFO("Built wall at castle {}",
                                     selected_castle_);
                        } else if (result == CommandResult::InsufficientGold) {
                            LOG_WARN("Insufficient gold to build wall");
                        } else if (result == CommandResult::AlreadyBuilt) {
                            LOG_WARN("Wall already built at castle {}",
                                     selected_castle_);
                        }
                    } else if (i == 3) {  // Build Barracks
                        auto result = ctx_.castle_command.build_facility(
                            selected_castle_, FacilityType::Barracks);
                        if (result == CommandResult::Success) {
                            LOG_INFO("Built barracks at castle {}",
                                     selected_castle_);
                        } else if (result == CommandResult::InsufficientGold) {
                            LOG_WARN("Insufficient gold to build barracks");
                        } else if (result == CommandResult::AlreadyBuilt) {
                            LOG_WARN("Barracks already built at castle {}",
                                     selected_castle_);
                        }
                    } else if (i == 4) {  // Back
                        ctx_.request_pop = true;
                    }
                }
            }

            ImGui::EndChild();
        }
    } else {
        ImGui::Text("Select a castle from the list");
    }

    ImGui::End();
}

// ===========================================================================
// BattleState
// ===========================================================================

BattleState::BattleState(GameContext& ctx) : ctx_(ctx) {}

void BattleState::enter() {
    LOG_INFO("BattleState: enter");
    battle_data_.init_battle();
    init_done_ = true;
    selected_unit_id_ = -1;
    show_move_range_ = false;
}

bool BattleState::update(float dt) {
    (void)dt;
    return true;
}

void BattleState::render() {
    if (!init_done_) return;

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

    // Convert grid to screen coordinates for rendering
    float screen_cx = win_pos.x + win_size.x * 0.5f;
    float screen_cy = win_pos.y + win_size.y * 0.5f;
    float cell_size = 40.0f;

    // Render terrain
    for (int32_t y = 0; y < battle_data_.grid_height(); ++y) {
        for (int32_t x = 0; x < battle_data_.grid_width(); ++x) {
            auto& cell = battle_data_.get_terrain(x, y);
            float sx = (float)x * cell_size + screen_cx -
                       (float)battle_data_.grid_width() / 2.0f * cell_size;
            float sy = (float)y * cell_size + screen_cy -
                       (float)battle_data_.grid_height() / 2.0f * cell_size;

            ImU32 terrain_col = IM_COL32(60, 100, 140, 255);  // Plain
            switch (cell.type) {
                case TerrainType::Forest:   terrain_col = IM_COL32(60, 140, 60, 255); break;
                case TerrainType::Mountain: terrain_col = IM_COL32(120, 100, 60, 255); break;
                case TerrainType::Water:    terrain_col = IM_COL32(60, 100, 180, 255); break;
                case TerrainType::Castle:   terrain_col = IM_COL32(180, 100, 60, 255); break;
                default: break;
            }
            draw->AddRectFilled({sx, sy}, {sx + cell_size, sy + cell_size},
                                terrain_col);
            draw->AddRect({sx, sy}, {sx + cell_size, sy + cell_size},
                          IM_COL32(40, 60, 80, 255), 0.0f, 0, 1.0f);
        }
    }

    // Render units
    for (auto& unit : battle_data_.units()) {
        if (unit.is_dead()) continue;

        float sx = (float)unit.grid_x * cell_size + screen_cx -
                   (float)battle_data_.grid_width() / 2.0f * cell_size +
                   cell_size * 0.5f;
        float sy = (float)unit.grid_y * cell_size + screen_cy -
                   (float)battle_data_.grid_height() / 2.0f * cell_size +
                   cell_size * 0.5f;

        ImU32 unit_col = unit.is_player
                             ? IM_COL32(100, 200, 100, 255)  // Player: green
                             : IM_COL32(200, 100, 100, 255);  // Enemy: red

        if (unit.id == (uint32_t)selected_unit_id_) {
            // Selection highlight
            draw->AddCircle({sx, sy}, cell_size * 0.6f,
                            IM_COL32(255, 255, 0, 200), 12, 3.0f);
        }

        draw->AddCircleFilled({sx, sy}, cell_size * 0.35f, unit_col, 10);

        // HP bar above unit
        float hp_pct = (float)unit.hp / (float)unit.max_hp;
        draw->AddRectFilled({sx - cell_size * 0.3f, sy - cell_size * 0.6f},
                            {sx - cell_size * 0.3f + cell_size * 0.6f * hp_pct,
                             sy - cell_size * 0.5f},
                            IM_COL32(0, 255, 0, 200));
        draw->AddRect({sx - cell_size * 0.3f, sy - cell_size * 0.6f},
                      {sx + cell_size * 0.3f, sy - cell_size * 0.5f},
                      IM_COL32(255, 255, 255, 150), 0.0f, 0, 1.0f);

        // Unit label
        draw->AddText(nullptr, 10.0f,
                      {sx - cell_size * 0.3f, sy + cell_size * 0.4f},
                      IM_COL32(255, 255, 255, 230), unit.name.c_str());
    }

    // UI panel on right
    ImGui::SetWindowPos({win_size.x - 320, 0}, ImGuiCond_Always);
    ImGui::SetWindowSize({320, win_size.y}, ImGuiCond_Always);
    ImGui::Begin("Battle Info", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Turn: %s",
                battle_data_.is_player_turn() ? "Player" : "Enemy");
    ImGui::Separator();

    if (selected_unit_id_ >= 0) {
        auto* unit = battle_data_.get_unit_at(0, 0);  // dummy
        // Find selected unit
        for (auto& u : battle_data_.units()) {
            if (u.id == (uint32_t)selected_unit_id_) {
                unit = &u;
                break;
            }
        }
        if (unit) {
            ImGui::Text("Selected: %s", unit->name.c_str());
            ImGui::Text("HP: %d/%d", unit->hp, unit->max_hp);
            ImGui::Text("ATK: %d  DEF: %d", unit->attack, unit->defense);
            ImGui::Text("Move: %d/%d", unit->move_left, unit->move);
            ImGui::Separator();

            if (!unit->has_acted && unit->move_left > 0) {
                if (ImGui::Button("Move")) {
                    show_move_range_ = !show_move_range_;
                }
            }
            if (!unit->has_acted) {
                if (ImGui::Button("Attack")) {
                    // Attack mode - handled in click
                }
            }
            if (ImGui::Button("End Turn")) {
                battle_data_.next_turn();
                selected_unit_id_ = -1;
                show_move_range_ = false;
            }
        }
    } else {
        ImGui::Text("Select a unit");
        if (ImGui::Button("End Turn")) {
            battle_data_.next_turn();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Return to Map")) {
        ctx_.request_pop = true;
    }

    ImGui::End();

    // Handle mouse clicks for unit selection and movement/attack
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        // Convert screen to grid
        float mx = mouse.x - screen_cx +
                   (float)battle_data_.grid_width() / 2.0f * cell_size;
        float my = mouse.y - screen_cy +
                   (float)battle_data_.grid_height() / 2.0f * cell_size;
        int32_t gx = (int32_t)(mx / cell_size);
        int32_t gy = (int32_t)(my / cell_size);

        auto* unit = battle_data_.get_unit_at(gx, gy);
        if (unit) {
            if (unit->is_player && !unit->has_acted) {
                selected_unit_id_ = unit->id;
                show_move_range_ = true;
            } else if (selected_unit_id_ >= 0) {
                // Try to attack selected unit
                auto* attacker = battle_data_.get_unit_at(0, 0);
                for (auto& u : battle_data_.units()) {
                    if (u.id == (uint32_t)selected_unit_id_) {
                        attacker = &u;
                        break;
                    }
                }
                if (attacker && !attacker->has_acted &&
                    battle_data_.is_in_attack_range(attacker->grid_x,
                                                    attacker->grid_y, gx, gy)) {
                    battle_data_.attack(*attacker, *unit);
                    attacker->has_acted = true;
                    attacker->move_left = 0;
                    selected_unit_id_ = -1;
                    show_move_range_ = false;
                }
            }
        } else if (selected_unit_id_ >= 0) {
            // Try to move selected unit
            auto* attacker = battle_data_.get_unit_at(0, 0);
            for (auto& u : battle_data_.units()) {
                if (u.id == (uint32_t)selected_unit_id_) {
                    attacker = &u;
                    break;
                }
            }
            if (attacker && !attacker->has_acted &&
                attacker->move_left > 0 &&
                battle_data_.is_position_empty(gx, gy)) {
                int32_t dist = std::abs(attacker->grid_x - gx) +
                               std::abs(attacker->grid_y - gy);
                int32_t cost = battle_data_.movement_cost(gx, gy);
                if (dist == 1 && cost <= attacker->move_left) {
                    // Move unit
                    terrain_grid_cell_update(battle_data_, attacker, gx, gy);
                    attacker->move_left -= cost;
                    if (attacker->move_left <= 0) {
                        attacker->has_acted = true;
                    }
                }
            }
        }
    }

    ImGui::End();
}

// Helper to update terrain grid when unit moves
static void terrain_grid_cell_update(BattleStateData& bs, BattleUnit* unit,
                                     int32_t new_x, int32_t new_y) {
    // Clear old position
    auto& old_cell = bs.get_terrain(unit->grid_x, unit->grid_y);
    if (old_cell.unit_id == unit->id) {
        old_cell.unit_id = 0;
    }
    // Set new position
    auto& new_cell = bs.get_terrain(new_x, new_y);
    new_cell.unit_id = unit->id;
    unit->grid_x = new_x;
    unit->grid_y = new_y;
}

} // namespace tensyoki

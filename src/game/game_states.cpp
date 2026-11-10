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
    show_load_menu_ = false;
    selected_save_slot_ = 1;
}

bool MainMenuState::update(float dt) {
    (void)dt;
    return true;
}

void MainMenuState::render() {
    if (show_load_menu_) {
        render_load_menu();
        return;
    }

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
            } else if (i == 1) {
                show_load_menu_ = true;
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
        } else if (selected_item_ == 1) {
            show_load_menu_ = true;
        } else if (selected_item_ == 2) {
            ctx_.request_pop = true;
        }
    }
}

void MainMenuState::render_load_menu() {
    ImGui::SetNextWindowPos({400, 300}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({500, 400}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Load Game", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove);

    ImGui::Text("Load Game");
    ImGui::Separator();

    auto saves = ctx_.save_system.list_saves();
    if (saves.empty()) {
        ImGui::Text("No save files found.");
    } else {
        for (const auto& slot : saves) {
            std::string label = fmt::format(
                "Slot {}: {} - {}", slot.slot, slot.save_name, slot.date_string);
            bool selected = (selected_save_slot_ == slot.slot);
            if (ImGui::Selectable(label.c_str(), selected)) {
                selected_save_slot_ = slot.slot;
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Load Selected", {-1, 0})) {
            GameSaveData save_data;
            if (ctx_.save_system.load(selected_save_slot_, save_data)) {
                // Load game state
                ctx_.date = GameDate{save_data.date_year, save_data.date_month,
                                     save_data.date_day};
                ctx_.castle_command.set_castles_data(save_data.castles);
                ctx_.pending_push = GameContext::PendingState::MapView;
                show_load_menu_ = false;
                LOG_INFO("MainMenuState: loaded save slot %d",
                         selected_save_slot_);
            } else {
                LOG_WARN("MainMenuState: failed to load save slot %d",
                         selected_save_slot_);
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Back", {-1, 0})) {
        show_load_menu_ = false;
    }

    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        show_load_menu_ = false;
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
    last_month_ = ctx_.date.month;
}

bool MapViewState::update(float dt) {
    map_view_.update(dt, *ctx_.renderer);

    // Sync castle data from castle_command (facilities, garrison, etc.)
    if (ctx_.sndata) {
        map_view_.update_castle_data(ctx_.castle_command);
    }

    // Process monthly resource production when month changes
    if (ctx_.sndata && ctx_.date.month != last_month_) {
        last_month_ = ctx_.date.month;
        ctx_.castle_command.process_monthly_income();
        ctx_.castle_command.process_auto_recruit();
        ctx_.castle_command.process_region_control();
        LOG_INFO("MapViewState: processed monthly income for month %d",
                 ctx_.date.month);
    }

    // Process UI request flags from map_view_
    if (map_view_.request_castle_command) {
        map_view_.clear_requests();
        auto sel = map_view_.selected_castle();
        if (sel) {
            ctx_.selected_castle_id = *sel;
            ctx_.pending_push = GameContext::PendingState::CastleCommand;
        }
    }
    if (map_view_.request_battle) {
        map_view_.clear_requests();
        if (map_view_.attack_from_castle && map_view_.attack_target_castle) {
            // Campaign battle: from specific castle to target
            ctx_.selected_castle_id = *map_view_.attack_from_castle;
            ctx_.battle_target_castle = *map_view_.attack_target_castle;
            map_view_.attack_from_castle.reset();
            map_view_.attack_target_castle.reset();
        } else {
            // Direct battle from selected castle
            auto sel = map_view_.selected_castle();
            if (sel) {
                ctx_.selected_castle_id = *sel;
                ctx_.battle_target_castle = *sel;
            }
        }
        ctx_.pending_push = GameContext::PendingState::Battle;
    }

    return true;
}

void MapViewState::render() {
    if (!init_done_ && ctx_.renderer && ctx_.sndata && ctx_.tex_cache) {
        if (map_view_.init(*ctx_.sndata, *ctx_.tex_cache, *ctx_.renderer,
                           ctx_.grp_archive.get())) {
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
        map_view_.handle_click(mouse.x, mouse.y, *ctx_.renderer);
    }

    ImGui::End();

    // Render UI overlays (tooltips, castle details, camera info)
    map_view_.render_ui(*ctx_.renderer);

    // Render castle info panel (date, stats, selected castle summary)
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({340, 420}, ImGuiCond_Always);
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
        }
    } else {
        ImGui::Text("Click on a castle to select");
    }
    ImGui::End();

    // Save/Load panel
    ImGui::SetNextWindowPos({(float)ctx_.renderer->get_width() - 200, 200}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({190, 120}, ImGuiCond_Always);
    ImGui::Begin("Save/Load", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Save Game:");
    static int save_slot = 1;
    ImGui::InputInt("##save_slot", &save_slot, 1, 5);
    if (save_slot < 1) save_slot = 1;
    if (save_slot > 10) save_slot = 10;
    if (ImGui::Button("Save", {-1, 0})) {
        GameSaveData save_data;
        save_data.save_name = "Save " + std::to_string(save_slot);
        save_data.date_year = ctx_.date.year;
        save_data.date_month = ctx_.date.month;
        save_data.date_day = ctx_.date.day;
        save_data.castles = ctx_.castle_command.castles();
        save_data.player_castle_count = ctx_.castle_command.player_castle_count();
        save_data.total_castles = static_cast<int32_t>(ctx_.castle_command.castles().size());
        if (ctx_.save_system.save(save_slot, save_data)) {
            LOG_INFO("MapViewState: saved game to slot %d", save_slot);
        } else {
            LOG_WARN("MapViewState: failed to save to slot %d", save_slot);
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Load Game", {-1, 0})) {
        // Switch to main menu to load
        ctx_.pending_push = GameContext::PendingState::MainMenu;
        // Set a flag to auto-open load menu
        // For simplicity, user will need to select Load Game from menu
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
// CastleCommandState
// ===========================================================================

CastleCommandState::CastleCommandState(GameContext& ctx) : ctx_(ctx) {}

void CastleCommandState::enter() {
    LOG_INFO("CastleCommandState: enter");
    selected_castle_ = -1;
    selected_tab_ = 0;
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
    ImGui::BeginChild("CastleList", ImVec2(300, 220), true);
    for (const auto& castle : ctx_.castle_command.castles()) {
        bool selected = (selected_castle_ == static_cast<int>(castle.index));
        std::string lord_name = "";
        if (castle.lord_officer_id != 0 && ctx_.sndata) {
            for (const auto& officer : ctx_.sndata->officers) {
                if (officer.id == castle.lord_officer_id) {
                    lord_name = officer.name_utf8.empty()
                                    ? officer.name_raw
                                    : officer.name_utf8;
                    break;
                }
            }
        }
        std::string label = fmt::format(
            "[{}] {} (Lv.{}){}",
            castle.index,
            castle.name_utf8.empty() ? castle.name : castle.name_utf8,
            castle.level,
            lord_name.empty() ? "" : fmt::format(" [Lord: {}]", lord_name));
        if (ImGui::Selectable(label.c_str(), selected)) {
            selected_castle_ = castle.index;
            selected_tab_ = 0;
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
            ImGui::Text("  Storehouse: %s",
                        castle->has_storehouse ? "Yes" : "No");
            ImGui::Text("  Market: %s", castle->has_market ? "Yes" : "No");
            ImGui::Separator();
            ImGui::Text("Lord: %s",
                        castle->lord_officer_id != 0
                            ? ctx_.castle_command.get_officer_name(
                                  castle->lord_officer_id)
                                .c_str()
                            : "(none)");
            ImGui::Separator();

            // Tab buttons for command groups
            const char* tabs[] = {"Recruit", "Build", "Officer", "Upgrade"};
            for (int i = 0; i < 4; ++i) {
                bool selected = (selected_tab_ == i);
                if (ImGui::Selectable(tabs[i], selected)) {
                    selected_tab_ = i;
                }
                if (i < 3) ImGui::SameLine();
            }
            ImGui::Separator();

            if (selected_tab_ == 0) {
                // ---- Recruit Troops ----
                ImGui::Text("Recruit Troops:");
                static int recruit_amount = 10;
                ImGui::InputInt("Amount", &recruit_amount, 10, 50);
                if (recruit_amount < 1) recruit_amount = 1;
                ImGui::SameLine();
                ImGui::Text("Cost: %d gold",
                            recruit_amount * ctx_.castle_command.recruitment_cost_per_troop());

                if (ImGui::Button("Recruit", {-1, 0})) {
                    auto result = ctx_.castle_command.recruit_troops(
                        selected_castle_, recruit_amount);
                    if (result == CommandResult::Success) {
                        LOG_INFO("Recruited {} troops at castle {}",
                                 recruit_amount, selected_castle_);
                    } else if (result == CommandResult::InsufficientGold) {
                        LOG_WARN("Insufficient gold to recruit troops");
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Auto-Recruit (Max)", {-1, 0})) {
                    auto result = ctx_.castle_command.auto_recruit(
                        selected_castle_);
                    if (result == CommandResult::Success) {
                        LOG_INFO("Auto-recruited troops at castle {}",
                                 selected_castle_);
                    } else if (result == CommandResult::InsufficientGold) {
                        LOG_WARN("Insufficient gold for auto-recruit");
                    }
                }

            } else if (selected_tab_ == 1) {
                // ---- Build Facilities ----
                ImGui::Text("Build Facilities:");

                struct FacilityBtn {
                    FacilityType type;
                    const char* label;
                };
                FacilityBtn facilities[] = {
                    {FacilityType::Wall, "Build Wall"},
                    {FacilityType::Barracks, "Build Barracks"},
                    {FacilityType::Storehouse, "Build Storehouse"},
                    {FacilityType::Market, "Build Market"},
                };

                for (const auto& fb : facilities) {
                    bool already = false;
                    switch (fb.type) {
                        case FacilityType::Wall:      already = castle->has_wall; break;
                        case FacilityType::Barracks:  already = castle->has_barracks; break;
                        case FacilityType::Storehouse:already = castle->has_storehouse; break;
                        case FacilityType::Market:    already = castle->has_market; break;
                        case FacilityType::None:      already = true; break;
                    }
                    int cost = ctx_.castle_command.facility_cost(fb.type);
                    bool affordable = castle->gold >= cost;

                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          already ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                                                  : (affordable ? ImVec4(1, 1, 1, 1)
                                                                : ImVec4(1, 0.5f, 0.5f, 1.0f)));
                    std::string btn_label = fmt::format("{} ({} gold){}",
                                                        fb.label, cost,
                                                        already ? " [Built]"
                                                                : (affordable ? "" : " [Poor]"));
                    if (ImGui::Button(btn_label.c_str(), {-1, 0}) &&
                        !already && affordable) {
                        auto result = ctx_.castle_command.build_facility(
                            selected_castle_, fb.type);
                        if (result == CommandResult::Success) {
                            LOG_INFO("Built {} at castle {}", fb.label,
                                     selected_castle_);
                        } else if (result == CommandResult::InsufficientGold) {
                            LOG_WARN("Insufficient gold to build {}", fb.label);
                        } else if (result == CommandResult::AlreadyBuilt) {
                            LOG_WARN("{} already built at castle {}", fb.label,
                                     selected_castle_);
                        }
                    }
                    ImGui::PopStyleColor();
                }

            } else if (selected_tab_ == 2) {
                // ---- Officer Assignment ----
                ImGui::Text("Appoint Lord:");

                // Show current lord
                if (castle->lord_officer_id != 0) {
                    std::string lord_name =
                        ctx_.castle_command.get_officer_name(
                            castle->lord_officer_id);
                    ImGui::Text("Current Lord: %s",
                                lord_name.empty() ? "(unknown)"
                                                  : lord_name.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Dismiss Lord", {-1, 0})) {
                        auto result = ctx_.castle_command.dismiss_lord(
                            selected_castle_);
                        if (result == CommandResult::Success) {
                            LOG_INFO("Dismissed lord from castle {}",
                                     selected_castle_);
                        }
                    }
                } else {
                    ImGui::Text("Current Lord: (none)");
                }

                ImGui::Separator();
                ImGui::Text("Available Officers:");

                auto available = ctx_.castle_command.get_available_officers(
                    static_cast<uint32_t>(selected_castle_));
                if (available.empty()) {
                    ImGui::TextDisabled("  (no unassigned officers)");
                } else {
                    for (uint32_t off_id : available) {
                        std::string off_name =
                            ctx_.castle_command.get_officer_name(off_id);
                        if (off_name.empty()) off_name = fmt::format("ID {}", off_id);
                        if (ImGui::Selectable(off_name.c_str())) {
                            auto result = ctx_.castle_command.assign_lord(
                                selected_castle_, off_id);
                            if (result == CommandResult::Success) {
                                LOG_INFO("Assigned officer {} to castle {}",
                                         off_id, selected_castle_);
                            }
                        }
                    }
                }

            } else if (selected_tab_ == 3) {
                // ---- Upgrade Castle ----
                ImGui::Text("Upgrade Castle:");
                int cost = ctx_.castle_command.castle_upgrade_cost(
                    static_cast<uint32_t>(selected_castle_));
                ImGui::Text("Current Level: %d", castle->level);
                ImGui::Text("Next Level: %d", castle->level + 1);
                ImGui::Text("Cost: %d gold", cost);
                ImGui::SameLine();
                ImGui::Text(castle->gold >= cost ? "(Affordable)"
                                                 : "(Insufficient Gold)");
                if (ImGui::Button("Upgrade", {-1, 0})) {
                    auto result = ctx_.castle_command.upgrade_castle(
                        static_cast<uint32_t>(selected_castle_));
                    if (result == CommandResult::Success) {
                        LOG_INFO("Upgraded castle {} to level {}",
                                 selected_castle_, castle->level);
                    } else if (result == CommandResult::InsufficientGold) {
                        LOG_WARN("Insufficient gold to upgrade castle");
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Back to Map", {-1, 0})) {
                ctx_.request_pop = true;
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

    // Check if we have a target castle for map-to-battle linkage
    if (ctx_.battle_target_castle.has_value() && ctx_.sndata) {
        battle_data_.init_from_castle(*ctx_.battle_target_castle, true);
        LOG_INFO("BattleState: initialized from castle {}",
                 *ctx_.battle_target_castle);
        ctx_.battle_target_castle.reset();  // Consume the target
    } else {
        battle_data_.init_battle();
    }

    init_done_ = true;
    selected_unit_id_ = -1;
    show_move_range_ = false;
    show_skill_range_ = false;
    selected_action_ = 0;
    selected_skill_idx_ = 0;
    show_settlement_ = false;
}

bool BattleState::update(float dt) {
    (void)dt;

    // Recalculate movable cells when a unit is selected and move range is shown
    if (show_move_range_ && selected_unit_id_ >= 0) {
        movable_cells_.clear();
        BattleUnit* unit = nullptr;
        for (auto& u : battle_data_.units()) {
            if (u.id == (uint32_t)selected_unit_id_) {
                unit = &u;
                break;
            }
        }
        if (unit && !unit->has_acted && unit->move_left > 0) {
            // Find all reachable cells using BFS limited by move_left
            int32_t gx = unit->grid_x;
            int32_t gy = unit->grid_y;
            int32_t max_cost = unit->move_left;

            std::vector<std::vector<bool>> visited(
                battle_data_.grid_height(),
                std::vector<bool>(battle_data_.grid_width(), false));
            std::vector<std::vector<int32_t>> cost_so_far(
                battle_data_.grid_height(),
                std::vector<int32_t>(battle_data_.grid_width(), 99999));
            std::vector<std::pair<int32_t, int32_t>> queue;
            queue.push_back({gx, gy});
            visited[gy][gx] = true;
            cost_so_far[gy][gx] = 0;

            size_t qi = 0;
            while (qi < queue.size()) {
                auto [cx, cy] = queue[qi++];
                const int dx[4] = {0, 1, 0, -1};
                const int dy[4] = {-1, 0, 1, 0};
                for (int i = 0; i < 4; ++i) {
                    int32_t nx = cx + dx[i];
                    int32_t ny = cy + dy[i];
                    if (!battle_data_.is_valid_position(nx, ny)) continue;
                    if (!battle_data_.is_position_empty(nx, ny)) continue;
                    int32_t move_cost = battle_data_.movement_cost(nx, ny);
                    int32_t new_cost = cost_so_far[cy][cx] + move_cost;
                    if (new_cost > max_cost) continue;
                    if (visited[ny][nx]) continue;
                    visited[ny][nx] = true;
                    cost_so_far[ny][nx] = new_cost;
                    queue.push_back({nx, ny});
                    movable_cells_.push_back({nx, ny});
                }
            }
        }
    }

    // Recalculate melee attackable cells
    attackable_cells_.clear();
    // Recalculate ranged attackable cells
    ranged_attackable_cells_.clear();
    // Recalculate skill range cells
    skill_range_cells_.clear();
    if (selected_unit_id_ >= 0) {
        BattleUnit* unit = nullptr;
        for (auto& u : battle_data_.units()) {
            if (u.id == (uint32_t)selected_unit_id_) {
                unit = &u;
                break;
            }
        }
        if (unit && !unit->has_acted) {
            // Check all cells within attack range for enemies
            int32_t atk_range = unit->get_attack_range();
            for (auto& target : battle_data_.units()) {
                if (target.is_dead() || target.is_player == unit->is_player)
                    continue;
                int32_t dist = std::abs(unit->grid_x - target.grid_x) +
                               std::abs(unit->grid_y - target.grid_y);
                if (dist <= atk_range && dist >= 1) {
                    if (atk_range == 1) {
                        attackable_cells_.push_back({target.grid_x, target.grid_y});
                    } else {
                        ranged_attackable_cells_.push_back({target.grid_x, target.grid_y});
                    }
                }
            }

            // Calculate skill range if skill action is selected
            if (selected_action_ == 2) {
                auto skills = unit->get_available_skills();
                if (selected_skill_idx_ >= 0 &&
                    selected_skill_idx_ < static_cast<int>(skills.size())) {
                    const auto& skill = skills[selected_skill_idx_];
                    skill_range_cells_ = battle_data_.get_skill_range(*unit, skill);
                }
            }
        }
    }

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

    // --- Render movement range highlight ---
    if (show_move_range_ && selected_unit_id_ >= 0) {
        for (const auto& [mx, my] : movable_cells_) {
            float sx = (float)mx * cell_size + screen_cx -
                       (float)battle_data_.grid_width() / 2.0f * cell_size;
            float sy = (float)my * cell_size + screen_cy -
                       (float)battle_data_.grid_height() / 2.0f * cell_size;
            draw->AddRectFilled({sx, sy}, {sx + cell_size, sy + cell_size},
                                IM_COL32(100, 255, 100, 60));
            draw->AddRect({sx, sy}, {sx + cell_size, sy + cell_size},
                          IM_COL32(100, 255, 100, 150), 0.0f, 0, 1.0f);
        }
    }

    // --- Render melee attack range highlight (red) ---
    if (selected_unit_id_ >= 0 && selected_action_ == 0) {
        for (const auto& [ax, ay] : attackable_cells_) {
            float sx = (float)ax * cell_size + screen_cx -
                       (float)battle_data_.grid_width() / 2.0f * cell_size;
            float sy = (float)ay * cell_size + screen_cy -
                       (float)battle_data_.grid_height() / 2.0f * cell_size;
            draw->AddRectFilled({sx, sy}, {sx + cell_size, sy + cell_size},
                                IM_COL32(255, 80, 80, 90));
            draw->AddRect({sx, sy}, {sx + cell_size, sy + cell_size},
                          IM_COL32(255, 100, 100, 200), 0.0f, 0, 2.0f);
        }
    }

    // --- Render ranged attack range highlight (orange) ---
    if (selected_unit_id_ >= 0 && selected_action_ == 1) {
        for (const auto& [ax, ay] : ranged_attackable_cells_) {
            float sx = (float)ax * cell_size + screen_cx -
                       (float)battle_data_.grid_width() / 2.0f * cell_size;
            float sy = (float)ay * cell_size + screen_cy -
                       (float)battle_data_.grid_height() / 2.0f * cell_size;
            draw->AddRectFilled({sx, sy}, {sx + cell_size, sy + cell_size},
                                IM_COL32(255, 180, 80, 90));
            draw->AddRect({sx, sy}, {sx + cell_size, sy + cell_size},
                          IM_COL32(255, 160, 80, 200), 0.0f, 0, 2.0f);
        }
    }

    // --- Render skill range highlight (purple) ---
    if (selected_unit_id_ >= 0 && selected_action_ == 2 && show_skill_range_) {
        BattleUnit* unit = nullptr;
        for (auto& u : battle_data_.units()) {
            if (u.id == (uint32_t)selected_unit_id_) {
                unit = &u;
                break;
            }
        }
        if (unit) {
            auto skills = unit->get_available_skills();
            if (selected_skill_idx_ >= 0 &&
                selected_skill_idx_ < static_cast<int>(skills.size())) {
                const auto& skill = skills[selected_skill_idx_];
                ImU32 skill_col = skill.target_enemy
                    ? IM_COL32(255, 100, 255, 90)
                    : IM_COL32(100, 100, 255, 90);
                ImU32 skill_border = skill.target_enemy
                    ? IM_COL32(255, 100, 255, 200)
                    : IM_COL32(100, 100, 255, 200);
                for (const auto& [sx, sy] : skill_range_cells_) {
                    float fx = (float)sx * cell_size + screen_cx -
                               (float)battle_data_.grid_width() / 2.0f * cell_size;
                    float fy = (float)sy * cell_size + screen_cy -
                               (float)battle_data_.grid_height() / 2.0f * cell_size;
                    draw->AddRectFilled({fx, fy}, {fx + cell_size, fy + cell_size},
                                        skill_col);
                    draw->AddRect({fx, fy}, {fx + cell_size, fy + cell_size},
                                  skill_border, 0.0f, 0, 2.0f);
                }
            }
        }
    }

    // --- Render terrain ---
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

    // --- Render units ---
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

        // Unit type color accent
        if (unit.unit_type == UnitType::Archer)
            unit_col = IM_COL32(200, 200, 100, 255);  // Archer: yellow
        else if (unit.unit_type == UnitType::Cavalry)
            unit_col = IM_COL32(100, 100, 200, 255);  // Cavalry: blue

        // Dim units that have acted
        if (unit.has_acted) {
            unit_col = IM_COL32(
                (unit_col >> 24) & 0xFF,
                ((unit_col >> 16) & 0xFF) * 2 / 3,
                ((unit_col >> 8) & 0xFF) * 2 / 3,
                (unit_col & 0xFF) * 2 / 3);
        }

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

        // Morale bar below HP bar
        float mor_pct = (float)unit.morale / (float)unit.max_morale;
        draw->AddRectFilled({sx - cell_size * 0.3f, sy - cell_size * 0.5f},
                            {sx - cell_size * 0.3f + cell_size * 0.6f * mor_pct,
                             sy - cell_size * 0.45f},
                            IM_COL32(255, 215, 0, 200));
        draw->AddRect({sx - cell_size * 0.3f, sy - cell_size * 0.5f},
                      {sx + cell_size * 0.3f, sy - cell_size * 0.45f},
                      IM_COL32(255, 255, 255, 100), 0.0f, 0, 1.0f);

        // Move left indicator
        if (unit.is_player && !unit.has_acted && unit.move_left > 0) {
            float mv_pct = (float)unit.move_left / (float)unit.move;
            draw->AddRectFilled({sx - cell_size * 0.3f, sy - cell_size * 0.4f},
                                {sx - cell_size * 0.3f + cell_size * 0.6f * mv_pct,
                                 sy - cell_size * 0.35f},
                                IM_COL32(0, 200, 255, 180));
        }

        // Unit label with type
        std::string type_str;
        switch (unit.unit_type) {
            case UnitType::Infantry:  type_str = "[Inf]"; break;
            case UnitType::Cavalry:   type_str = "[Cav]"; break;
            case UnitType::Archer:    type_str = "[Arc]"; break;
        }
        std::string label = type_str + " " + unit.name;
        draw->AddText(static_cast<ImFont*>(nullptr), 10.0f,
                      {sx - cell_size * 0.3f, sy + cell_size * 0.45f},
                      IM_COL32(255, 255, 255, 230), label.c_str());
    }

    // --- Render battle result ---
    if (battle_data_.result() != BattleResult::Ongoing) {
        const char* result_text;
        ImU32 result_col;
        switch (battle_data_.result()) {
            case BattleResult::PlayerVictory:
                result_text = "VICTORY";
                result_col = IM_COL32(100, 255, 100, 255);
                break;
            case BattleResult::PlayerDefeat:
                result_text = "DEFEAT";
                result_col = IM_COL32(255, 100, 100, 255);
                break;
            default:
                result_text = "DRAW";
                result_col = IM_COL32(255, 255, 100, 255);
                break;
        }
        draw->AddText(static_cast<ImFont*>(nullptr), 24.0f,
                      {win_pos.x + win_size.x * 0.5f - 60.0f,
                       win_pos.y + win_size.y * 0.5f - 12.0f},
                      result_col, result_text);
    }

    // --- UI panel on right ---
    ImGui::SetWindowPos({win_size.x - 320, 0}, ImGuiCond_Always);
    ImGui::SetWindowSize({320, win_size.y}, ImGuiCond_Always);
    ImGui::Begin("Battle Info", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Turn: %s",
                battle_data_.is_player_turn() ? "Player" : "Enemy");
    ImGui::Separator();

    if (selected_unit_id_ >= 0) {
        BattleUnit* unit = nullptr;
        for (auto& u : battle_data_.units()) {
            if (u.id == (uint32_t)selected_unit_id_) {
                unit = &u;
                break;
            }
        }
        if (unit) {
            ImGui::Text("Selected: %s", unit->name.c_str());
            std::string type_str;
            switch (unit->unit_type) {
                case UnitType::Infantry:  type_str = "Infantry"; break;
                case UnitType::Cavalry:   type_str = "Cavalry"; break;
                case UnitType::Archer:    type_str = "Archer"; break;
            }
            ImGui::Text("Type: %s  Level: %d", type_str.c_str(), unit->level);
            ImGui::Text("HP: %d/%d", unit->hp, unit->max_hp);
            ImGui::Text("Morale: %d/%d", unit->morale, unit->max_morale);
            ImGui::Text("ATK: %d  DEF: %d", unit->effective_attack(),
                        unit->effective_defense());
            ImGui::Text("Move: %d/%d", unit->move_left, unit->move);
            ImGui::Text("Range: %d", unit->get_attack_range());
            ImGui::Separator();

            // Action selection
            ImGui::Text("Action:");
            const char* actions[] = {"Melee", "Ranged", "Skill", "Wait"};
            for (int i = 0; i < 4; ++i) {
                bool selected = (selected_action_ == i);
                bool available = false;
                if (i == 0) available = !unit->has_acted && !attackable_cells_.empty();
                if (i == 1) available = !unit->has_acted &&
                                        unit->unit_type == UnitType::Archer &&
                                        !ranged_attackable_cells_.empty();
                if (i == 3) available = !unit->has_acted;  // Wait always available

                if (!available) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                if (ImGui::Selectable(actions[i], selected)) {
                    selected_action_ = i;
                    show_move_range_ = false;
                    show_skill_range_ = false;
                }
                if (!available) ImGui::PopStyleColor();
                if (i < 3) ImGui::SameLine();
            }

            // Skill selection (only when Skill action is selected)
            if (selected_action_ == 2 && !unit->has_acted) {
                auto skills = unit->get_available_skills();
                if (!skills.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Select Skill:");
                    for (size_t i = 0; i < skills.size(); ++i) {
                        const auto& s = skills[i];
                        bool skill_selected = (selected_skill_idx_ == (int)i);
                        bool affordable = unit->morale >= s.morale_cost;
                        std::string skill_label = fmt::format(
                            "{} (Morale: {}){}", s.name, s.morale_cost,
                            affordable ? "" : " [Insufficient Morale]");
                        if (!affordable) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        if (ImGui::Selectable(skill_label.c_str(), skill_selected)) {
                            selected_skill_idx_ = (int)i;
                            show_skill_range_ = true;
                            show_move_range_ = false;
                        }
                        if (!affordable) ImGui::PopStyleColor();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", s.description.c_str());
                    }
                }
            }

            if (!unit->has_acted && unit->move_left > 0) {
                ImGui::Separator();
                if (ImGui::Button("Move Range", {-1, 0})) {
                    show_move_range_ = !show_move_range_;
                    selected_action_ = 0;
                }
            }

            ImGui::Separator();
            if (ImGui::Button("End Turn")) {
                battle_data_.next_turn();
                selected_unit_id_ = -1;
                show_move_range_ = false;
                show_skill_range_ = false;
                selected_action_ = 0;
                selected_skill_idx_ = 0;
                movable_cells_.clear();
                attackable_cells_.clear();
                ranged_attackable_cells_.clear();
                skill_range_cells_.clear();
            }
        }
    } else {
        ImGui::Text("Select a unit");
        if (ImGui::Button("End Turn")) {
            battle_data_.next_turn();
            selected_action_ = 0;
        }
    }

    ImGui::Separator();

    // Battle log from events
    ImGui::Text("Battle Log:");
    ImGui::BeginChild("BattleLog", ImVec2(0, 150), true);
    for (const auto& evt : battle_data_.events()) {
        ImVec4 col = evt.is_player_action
                         ? ImVec4(0.8f, 0.9f, 1.0f, 1.0f)
                         : ImVec4(1.0f, 0.7f, 0.7f, 1.0f);
        ImGui::TextColored(col, "%s", evt.message.c_str());
    }
    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Return to Map", {-1, 0})) {
        ctx_.request_pop = true;
    }

    // Battle settlement dialog
    if (battle_data_.result() != BattleResult::Ongoing && !show_settlement_) {
        show_settlement_ = true;
    }
    if (show_settlement_) {
        auto settlement = battle_data_.get_settlement();
        ImGui::OpenPopup("Battle Settlement");
        if (ImGui::BeginPopupModal("Battle Settlement", &show_settlement_,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* result_text;
            ImVec4 result_col;
            switch (settlement.result) {
                case BattleResult::PlayerVictory:
                    result_text = "VICTORY";
                    result_col = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                    break;
                case BattleResult::PlayerDefeat:
                    result_text = "DEFEAT";
                    result_col = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    break;
                default:
                    result_text = "DRAW";
                    result_col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                    break;
            }
            ImGui::TextColored(result_col, "Battle Result: %s", result_text);
            ImGui::Separator();
            ImGui::Text("Enemies Defeated: %d", settlement.enemies_defeated);
            ImGui::Text("Enemies Captured: %d", settlement.enemies_captured);
            ImGui::Text("Player Units Lost: %d", settlement.player_units_lost);
            ImGui::Text("Total EXP Gained: %d", settlement.total_exp_gained);
            ImGui::Text("Total Gold Gained: %d", settlement.total_gold_gained);
            ImGui::Separator();
            if (!settlement.level_ups.empty()) {
                ImGui::Text("Level Ups:");
                for (const auto& lu : settlement.level_ups) {
                    ImGui::BulletText("%s", lu.c_str());
                }
                ImGui::Separator();
            }
            if (!settlement.captured_officer_ids.empty()) {
                ImGui::Text("Captured Officers:");
                for (auto oid : settlement.captured_officer_ids) {
                    ImGui::BulletText("Officer ID %u", oid);
                }
                ImGui::Separator();
            }
            if (ImGui::Button("OK", {100, 0})) {
                ImGui::CloseCurrentPopup();
                show_settlement_ = false;
                ctx_.request_pop = true;
            }
            ImGui::EndPopup();
        }
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
                selected_action_ = 0;
            } else if (selected_unit_id_ >= 0) {
                // Try to use skill or attack selected unit
                BattleUnit* attacker = nullptr;
                for (auto& u : battle_data_.units()) {
                    if (u.id == (uint32_t)selected_unit_id_) {
                        attacker = &u;
                        break;
                    }
                }
                if (!attacker || attacker->has_acted) {
                    selected_unit_id_ = -1;
                    show_move_range_ = false;
                    show_skill_range_ = false;
                    attackable_cells_.clear();
                    ranged_attackable_cells_.clear();
                    skill_range_cells_.clear();
                } else if (selected_action_ == 0) {
                    // Melee attack
                    int32_t dist = std::abs(attacker->grid_x - gx) +
                                   std::abs(attacker->grid_y - gy);
                    if (dist <= 1) {
                        int dmg = battle_data_.attack(*attacker, *unit);
                        selected_unit_id_ = -1;
                        show_move_range_ = false;
                        show_skill_range_ = false;
                        selected_action_ = 0;
                        movable_cells_.clear();
                        attackable_cells_.clear();
                        ranged_attackable_cells_.clear();
                        skill_range_cells_.clear();
                    }
                } else if (selected_action_ == 1) {
                    // Ranged attack (archers only)
                    if (attacker->unit_type != UnitType::Archer) {
                        // Not an archer, can't use ranged
                    } else {
                        int32_t dist = std::abs(attacker->grid_x - gx) +
                                       std::abs(attacker->grid_y - gy);
                        if (dist >= 2 && dist <= 3) {
                            int dmg = battle_data_.ranged_attack(*attacker, *unit);
                            selected_unit_id_ = -1;
                            show_move_range_ = false;
                            show_skill_range_ = false;
                            selected_action_ = 0;
                            movable_cells_.clear();
                            attackable_cells_.clear();
                            ranged_attackable_cells_.clear();
                            skill_range_cells_.clear();
                        }
                    }
                } else if (selected_action_ == 2) {
                    // Skill usage
                    auto skills = attacker->get_available_skills();
                    if (selected_skill_idx_ >= 0 &&
                        selected_skill_idx_ < static_cast<int>(skills.size())) {
                        const auto& skill = skills[selected_skill_idx_];
                        if (battle_data_.is_in_skill_range(
                                attacker->grid_x, attacker->grid_y, gx, gy,
                                skill)) {
                            int dmg = battle_data_.use_skill(
                                *attacker, skill, gx, gy);
                            selected_unit_id_ = -1;
                            show_move_range_ = false;
                            show_skill_range_ = false;
                            selected_action_ = 0;
                            selected_skill_idx_ = 0;
                            movable_cells_.clear();
                            attackable_cells_.clear();
                            ranged_attackable_cells_.clear();
                            skill_range_cells_.clear();
                        }
                    }
                }
            }
        } else if (selected_unit_id_ >= 0) {
            // Try to move selected unit to clicked cell
            BattleUnit* attacker = nullptr;
            for (auto& u : battle_data_.units()) {
                if (u.id == (uint32_t)selected_unit_id_) {
                    attacker = &u;
                    break;
                }
            }
            if (attacker && !attacker->has_acted &&
                attacker->move_left > 0 &&
                battle_data_.is_position_empty(gx, gy)) {
                // Check if cell is in movable range
                bool can_move = false;
                for (const auto& [mx, my] : movable_cells_) {
                    if (mx == gx && my == gy) {
                        can_move = true;
                        break;
                    }
                }
                if (can_move) {
                    // Move unit
                    terrain_grid_cell_update(battle_data_, attacker, gx, gy);
                    int32_t cost = battle_data_.movement_cost(gx, gy);
                    attacker->move_left -= cost;
                    if (attacker->move_left <= 0) {
                        attacker->has_acted = true;
                    }
                    show_move_range_ = false;
                    show_skill_range_ = false;
                    movable_cells_.clear();
                    attackable_cells_.clear();
                    ranged_attackable_cells_.clear();
                    skill_range_cells_.clear();
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

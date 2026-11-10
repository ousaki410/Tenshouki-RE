/**
 * map_view.cpp - Strategic map view implementation
 *
 * Renders castles from sndata.dat on a 2D map with camera zoom/pan,
 * routes between castles, region boundaries, and click-to-select interaction.
 *
 * Enhancements:
 *   - City sprites from GT1G textures (fallback to colored dots)
 *   - Route distance labels and color-coded connections
 *   - Facility icons (wall, barracks, storehouse, market)
 *   - Hover tooltips and selected castle detail panel
 *   - Region-colored selection halos
 */

#include "map_view.hpp"
#include "core/logging.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <random>
#include <numeric>
#include <set>
#include <cctype>
#include <fmt/format.h>

namespace tensyoki {

MapView::MapView() = default;

bool MapView::init(const SndataArchive& sndata, GT1GTextureCache& tex_cache,
                   Renderer& renderer, const BinArchive* grp_archive) {
    tex_cache_ = &tex_cache;
    renderer_ = &renderer;
    grp_archive_ = grp_archive;

    castles_.clear();
    castles_.reserve(sndata.castle_count());

    for (const auto& castle : sndata.castles) {
        CastlePos cp;
        cp.index = castle.index;
        cp.name = castle.name_raw;
        cp.name_utf8 = castle.name_utf8.empty() ? castle.name_raw : castle.name_utf8;
        cp.region_id = 0;  // Will be assigned in build_regions()
        castles_.push_back(cp);
    }

    // Create 1x1 white pixel texture for fallback markers
    {
        Renderer::TextureCreateInfo ti{};
        ti.width = 1;
        ti.height = 1;
        ti.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
        uint8_t white[4] = {255, 255, 255, 255};
        ti.initial_data = white;
        ti.data_size = 4;
        white_tex_ = renderer.create_texture(ti);
    }

    generate_positions();
    build_regions();
    build_routes();

    // Load sprites for castles (using GT1G entries based on castle index)
    if (grp_archive_ && tex_cache_) {
        for (uint32_t i = 0; i < castles_.size(); ++i) {
            load_castle_sprite(i);
        }
    }

    // Set default campaign ownership: player owns castle 0, nearby castles
    // are enemy-controlled, others are neutral
    for (size_t i = 0; i < castles_.size(); ++i) {
        if (i == 0) {
            set_castle_owner(castles_[i].index, 1, true);  // Player starts with castle 0
        } else if (i < 3) {
            set_castle_owner(castles_[i].index, 2, false);  // Nearby enemy castles
        }
        // Others remain neutral (owner_id = 0)
    }

    LOG_INFO("MapView: {} castles, {} routes, {} regions",
             castles_.size(), routes_.size(), regions_.size());
    return true;
}

void MapView::update_castle_data(const CastleCommand& cmd) {
    for (auto& cp : castles_) {
        const CastleData* cd = cmd.get_castle(cp.index);
        if (cd) {
            cp.level = cd->level;
            cp.garrison = cd->garrison;
            cp.max_garrison = cd->max_garrison;
            cp.gold = cd->gold;
            cp.food = cd->food;
            cp.has_wall = cd->has_wall;
            cp.has_barracks = cd->has_barracks;
            cp.has_storehouse = cd->has_storehouse;
            cp.has_market = cd->has_market;
        }
    }
}

void MapView::load_castle_sprite(uint32_t castle_index) {
    if (!grp_archive_ || !tex_cache_) return;

    // Try to load a GT1G texture for this castle.
    // Castle index maps to a GT1G entry in res_grp.bin.
    // Entries 0-15 are GT1G, entries 16+ are LWC (nested).
    // We use castle_index % 16 to pick a GT1G entry as a city sprite.
    uint32_t entry = castle_index % 16;
    auto tex = tex_cache_->get_or_load(*renderer_, "res_grp.bin",
                                       *grp_archive_, entry);
    if (tex) {
        castles_[castle_index].sprite_tex = tex;
        castles_[castle_index].sprite_loaded = true;
    }
}

void MapView::generate_positions() {
    // Deterministic positions based on castle index.
    // Japan roughly spans: Hokkaido (north), Tohoku, Kanto, Chubu, Kansai,
    // Chugoku, Shikoku, Kyushu (south). We simulate regions by grouping
    // castles into ~8 clusters arranged roughly N->S, W->E.
    const uint32_t n = static_cast<uint32_t>(castles_.size());
    const uint32_t num_regions = 8;

    // Assign each castle to a region deterministically
    for (uint32_t i = 0; i < n; ++i) {
        castles_[i].region_id = i % num_regions;
    }

    // Region centers (roughly Japan-shaped)
    const float region_cx[8] = {-60, -30, 0, 30, -45, -15, 15, -30};
    const float region_cy[8] = {50, 30, 10, -10, 25, -5, -25, -40};
    const float region_spread[8] = {15, 12, 18, 15, 12, 10, 12, 15};

    std::mt19937 rng(12345);

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t region = castles_[i].region_id;
        // Sub-seed per castle within region for spread
        rng.seed(12345 + i * 7919 + region * 131);
        std::uniform_real_distribution<float> dist_x(-region_spread[region],
                                                     region_spread[region]);
        std::uniform_real_distribution<float> dist_y(-region_spread[region],
                                                     region_spread[region]);
        castles_[i].x = region_cx[region] + dist_x(rng);
        castles_[i].y = region_cy[region] + dist_y(rng);
    }
}

void MapView::build_regions() {
    regions_.clear();
    const uint32_t n = static_cast<uint32_t>(castles_.size());

    // Determine number of regions from max region_id
    uint32_t max_region = 0;
    for (const auto& c : castles_) {
        max_region = std::max(max_region, c.region_id);
    }

    const char* region_names[8] = {
        "Hokkaido", "Tohoku", "Kanto", "Chubu",
        "Kansai", "Chugoku", "Shikoku", "Kyushu"
    };
    const float region_colors[8][4] = {
        {0.2f, 0.4f, 0.8f, 0.25f},  // Hokkaido - blue
        {0.2f, 0.6f, 0.4f, 0.25f},  // Tohoku - green
        {0.8f, 0.6f, 0.2f, 0.25f},  // Kanto - orange
        {0.6f, 0.4f, 0.8f, 0.25f},  // Chubu - purple
        {0.8f, 0.2f, 0.4f, 0.25f},  // Kansai - red
        {0.4f, 0.8f, 0.8f, 0.25f},  // Chugoku - cyan
        {0.8f, 0.4f, 0.2f, 0.25f},  // Shikoku - orange-red
        {0.6f, 0.2f, 0.6f, 0.25f},  // Kyushu - purple-red
    };

    for (uint32_t r = 0; r <= max_region; ++r) {
        MapRegion region;
        region.id = r;
        if (r < 8) {
            region.name = region_names[r];
            region.color = {
                region_colors[r][0], region_colors[r][1],
                region_colors[r][2], region_colors[r][3]
            };
        } else {
            region.name = "Region " + std::to_string(r);
            region.color = {0.5f, 0.5f, 0.5f, 0.25f};
        }

        for (uint32_t i = 0; i < n; ++i) {
            if (castles_[i].region_id == r) {
                region.castle_indices.push_back(i);
            }
        }
        regions_.push_back(region);
    }
}

void MapView::build_routes() {
    routes_.clear();
    const uint32_t n = static_cast<uint32_t>(castles_.size());

    // Connect each castle to its nearest 3 neighbors within same region
    for (uint32_t i = 0; i < n; ++i) {
        struct Dist { uint32_t j; float d; };
        std::vector<Dist> dists;
        dists.reserve(n - 1);
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || castles_[j].region_id != castles_[i].region_id)
                continue;
            float dx = castles_[i].x - castles_[j].x;
            float dy = castles_[i].y - castles_[j].y;
            dists.push_back({j, dx * dx + dy * dy});
        }
        std::sort(dists.begin(), dists.end(),
                  [](const Dist& a, const Dist& b) { return a.d < b.d; });

        // Connect to nearest 3 within region
        for (uint32_t k = 0; k < 3 && k < dists.size(); ++k) {
            uint32_t j = dists[k].j;
            // Avoid duplicate routes
            bool exists = false;
            for (const auto& r : routes_) {
                if ((r.from == i && r.to == j) ||
                    (r.from == j && r.to == i)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                MapRoute route;
                route.from = i;
                route.to = j;
                route.distance = std::sqrt(dists[k].d);
                routes_.push_back(route);
            }
        }
    }
}

glm::vec2 MapView::map_to_screen(float mx, float my) const {
    float sx = (mx - camera_.x) * camera_.zoom;
    float sy = (my - camera_.y) * camera_.zoom;
    return {sx, sy};
}

glm::vec2 MapView::screen_to_map(float sx, float sy) const {
    float mx = sx / camera_.zoom + camera_.x;
    float my = sy / camera_.zoom + camera_.y;
    return {mx, my};
}

void MapView::handle_camera_input(Renderer& renderer, float dt) {
    float speed = 30.0f * dt / camera_.zoom;
    bool moved = false;

    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
        camera_.y -= speed; moved = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
        camera_.y += speed; moved = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
        camera_.x -= speed; moved = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
        camera_.x += speed; moved = true;
    }

    // Reset camera with Home key
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        camera_.x = 0.0f;
        camera_.y = 0.0f;
        camera_.zoom = 1.0f;
        moved = true;
    }

    if (moved) {
        LOG_DEBUG("Camera: x=%.1f y=%.1f zoom=%.2f", camera_.x, camera_.y,
                  camera_.zoom);
    }
}

void MapView::handle_mouse_wheel(Renderer& renderer, float y_offset) {
    float zoom_speed = 0.1f;
    float old_zoom = camera_.zoom;
    camera_.zoom += y_offset * zoom_speed;
    if (camera_.zoom < 0.3f) camera_.zoom = 0.3f;
    if (camera_.zoom > 4.0f) camera_.zoom = 4.0f;

    // Zoom toward mouse position
    if (camera_.zoom != old_zoom) {
        ImVec2 mouse = ImGui::GetMousePos();
        auto map_pos = screen_to_map(mouse.x, mouse.y);
        camera_.x = map_pos.x - (mouse.x - renderer.get_width() * 0.5f) / camera_.zoom;
        camera_.y = map_pos.y - (mouse.y - renderer.get_height() * 0.5f) / camera_.zoom;
    }
}

void MapView::update(float dt, Renderer& renderer) {
    handle_camera_input(renderer, dt);

    // Check for mouse wheel
    if (ImGui::GetIO().MouseWheel != 0.0f) {
        handle_mouse_wheel(renderer, ImGui::GetIO().MouseWheel);
    }

    // Update hover state
    ImVec2 mouse = ImGui::GetMousePos();
    auto map_pos = screen_to_map(mouse.x, mouse.y);

    float best_dist = 999999.0f;
    std::optional<uint32_t> best_idx;

    for (auto& cp : castles_) {
        cp.hovered = false;
        float dx = cp.x - map_pos.x;
        float dy = cp.y - map_pos.y;
        float dist = dx * dx + dy * dy;
        float threshold = 12.0f / camera_.zoom;
        if (dist < threshold * threshold && dist < best_dist) {
            best_dist = dist;
            best_idx = cp.index;
        }
    }

    if (best_idx) {
        castles_[*best_idx].hovered = true;
    }
}

void MapView::draw_castle_sprite(ImDrawList* draw, const CastlePos& cp,
                                  float screen_x, float screen_y, float size) {
    if (cp.sprite_loaded && cp.sprite_tex) {
        // Draw the GT1G texture as a square sprite
        GLuint tex_id = cp.sprite_tex->get_id();
        ImVec2 p_min{screen_x - size * 0.5f, screen_y - size * 0.5f};
        ImVec2 p_max{screen_x + size * 0.5f, screen_y + size * 0.5f};
        ImVec2 uv_min{0, 0};
        ImVec2 uv_max{1, 1};
        ImU32 col = cp.selected ? IM_COL32(255, 255, 255, 255)
                                : (cp.hovered ? IM_COL32(255, 255, 255, 230)
                                              : IM_COL32(255, 255, 255, 200));
        draw->AddImage(static_cast<ImTextureID>(tex_id),
                       p_min, p_max, uv_min, uv_max, col);
        // Draw selection/hover border
        ImU32 border_col = cp.selected
            ? IM_COL32(255, 200, 50, 255)
            : (cp.hovered ? IM_COL32(255, 255, 255, 255)
                          : IM_COL32(100, 100, 100, 150));
        draw->AddRect(p_min, p_max, border_col, 0.0f, 0, 1.5f);
    } else {
        // Fallback: colored dot
        float dot_size = size * 0.5f;
        ImU32 col = cp.selected
            ? IM_COL32(255, 200, 50, 255)   // selected: gold
            : (cp.hovered ? IM_COL32(255, 255, 255, 255)  // hovered: white
                          : IM_COL32(150, 150, 255, 255)); // normal: light blue
        ImVec2 center{screen_x, screen_y};
        draw->AddCircleFilled(center, dot_size, col, 10);
        draw->AddCircle(center, dot_size + 1.0f, IM_COL32(0, 0, 0, 150), 10, 1.5f);
    }

    // Draw facility icons around the castle (small markers)
    float icon_offset = size * 0.8f;
    if (cp.has_wall) {
        // Wall: small square
        ImVec2 p_min{screen_x - 2.0f, screen_y - icon_offset - 2.0f};
        ImVec2 p_max{screen_x + 2.0f, screen_y - icon_offset + 2.0f};
        draw->AddRectFilled(p_min, p_max, IM_COL32(180, 180, 180, 220));
    }
    if (cp.has_barracks) {
        // Barracks: small triangle pointing up
        ImVec2 p[3] = {
            {screen_x, screen_y - icon_offset - 3.0f},
            {screen_x - 3.0f, screen_y - icon_offset + 3.0f},
            {screen_x + 3.0f, screen_y - icon_offset + 3.0f},
        };
        draw->AddConvexPolyFilled(p, 3, IM_COL32(140, 100, 60, 220));
    }
    if (cp.has_storehouse) {
        // Storehouse: small hexagon (approximated as circle)
        draw->AddCircleFilled({screen_x, screen_y - icon_offset}, 2.5f,
                              IM_COL32(100, 180, 100, 220), 6);
    }
    if (cp.has_market) {
        // Market: small diamond
        ImVec2 p[4] = {
            {screen_x, screen_y - icon_offset - 3.0f},
            {screen_x + 3.0f, screen_y - icon_offset},
            {screen_x, screen_y - icon_offset + 3.0f},
            {screen_x - 3.0f, screen_y - icon_offset},
        };
        draw->AddConvexPolyFilled(p, 4, IM_COL32(220, 200, 100, 220));
    }

    // Draw garrison strength bar below castle
    if (cp.max_garrison > 0) {
        float bar_w = size * 1.5f;
        float bar_h = 3.0f;
        float fill = static_cast<float>(cp.garrison) / static_cast<float>(cp.max_garrison);
        ImVec2 bar_min{screen_x - bar_w * 0.5f, screen_y + size * 0.6f};
        ImVec2 bar_max{screen_x + bar_w * 0.5f, screen_y + size * 0.6f + bar_h};
        draw->AddRectFilled(bar_min, bar_max, IM_COL32(60, 60, 60, 180));
        ImVec2 fill_max{screen_x - bar_w * 0.5f + bar_w * fill,
                        screen_y + size * 0.6f + bar_h};
        draw->AddRectFilled(bar_min, fill_max, IM_COL32(100, 200, 100, 220));
    }
}

void MapView::draw_route(ImDrawList* draw, const MapRoute& route,
                          float screen_cx, float screen_cy) {
    const auto& ca = castles_[route.from];
    const auto& cb = castles_[route.to];
    auto pa = map_to_screen(ca.x, ca.y);
    auto pb = map_to_screen(cb.x, cb.y);
    pa.x += screen_cx; pa.y += screen_cy;
    pb.x += screen_cx; pb.y += screen_cy;

    // Color based on whether either endpoint is selected/hovered
    bool highlighted = ca.selected || cb.selected || ca.hovered || cb.hovered;
    ImU32 route_col = highlighted
        ? IM_COL32(255, 200, 50, 200)
        : IM_COL32(100, 100, 100, 120);
    float thickness = highlighted ? 2.5f : 1.5f;

    ImVec2 pa_im{pa.x, pa.y};
    ImVec2 pb_im{pb.x, pb.y};
    draw->AddLine(pa_im, pb_im, route_col, thickness);

    // Draw distance label at midpoint
    if (highlighted) {
        ImVec2 mid{(pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f};
        std::string dist_label = fmt::format("{:.0f}", route.distance);
        draw->AddText(static_cast<ImFont*>(nullptr), 10.0f,
                      {mid.x - 8.0f, mid.y - 6.0f},
                      IM_COL32(200, 200, 200, 180), dist_label.c_str());
    }
}

void MapView::render(Renderer& renderer) {
    if (castles_.empty()) return;

    int w = renderer.get_width();
    int h = renderer.get_height();
    float screen_cx = w * 0.5f;
    float screen_cy = h * 0.5f;

    ImDrawList* bg_draw = ImGui::GetBackgroundDrawList();
    ImDrawList* fg_draw = ImGui::GetForegroundDrawList();

    // --- Render region backgrounds first ---
    if (show_regions_) {
        for (const auto& region : regions_) {
            if (region.castle_indices.size() < 2) continue;

            // Compute region center
            ImVec2 center{0, 0};
            int count = 0;
            for (uint32_t ci : region.castle_indices) {
                auto p = map_to_screen(castles_[ci].x, castles_[ci].y);
                center.x += p.x + screen_cx;
                center.y += p.y + screen_cy;
                count++;
            }
            if (count == 0) continue;
            center.x /= count;
            center.y /= count;

            ImVec4 col_vec{region.color.x, region.color.y, region.color.z, region.color.w};
            ImU32 col = ImGui::ColorConvertFloat4ToU32(col_vec);
            ImVec2 center_im{center.x, center.y};
            float region_radius = 70.0f * camera_.zoom;
            bg_draw->AddCircleFilled(center_im, region_radius, col, 12);
        }
    }

    // --- Render routes ---
    if (show_routes_) {
        for (const auto& r : routes_) {
            // Only draw routes where both endpoints are visible
            if (!is_castle_visible(r.from) || !is_castle_visible(r.to)) continue;
            draw_route(bg_draw, r, screen_cx, screen_cy);
        }

        // Draw planned route (route planning overlay)
        if (route_from_ && route_to_) {
            draw_planned_route(fg_draw, screen_cx, screen_cy);
        }
    }

    // --- Render castle markers ---
    for (auto& cp : castles_) {
        if (!is_castle_visible(cp.index)) continue;

        auto p = map_to_screen(cp.x, cp.y);
        p.x += screen_cx;
        p.y += screen_cy;

        // Skip if off-screen
        if (p.x < -100 || p.x > w + 100 || p.y < -100 || p.y > h + 100) continue;

        float size = 16.0f * camera_.zoom;
        draw_castle_sprite(fg_draw, cp, p.x, p.y, size);

        // Draw ownership indicator (ownership dot below castle)
        float dot_size = 3.0f * camera_.zoom;
        ImU32 owner_col = cp.is_player_owned
            ? IM_COL32(0, 255, 0, 220)     // Player: green
            : (cp.owner_id != 0
                ? IM_COL32(255, 0, 0, 220)  // Enemy: red
                : IM_COL32(128, 128, 128, 220));  // Neutral: gray
        fg_draw->AddCircleFilled({p.x, p.y + size * 0.7f}, dot_size, owner_col, 6);
    }

    // --- Render selected castle highlight (halo) ---
    if (selected_) {
        for (const auto& cp : castles_) {
            if (cp.index == *selected_) {
                auto p = map_to_screen(cp.x, cp.y);
                p.x += screen_cx;
                p.y += screen_cy;
                // Pulsing halo
                float pulse = 1.0f + 0.3f * std::sinf(ImGui::GetTime() * 4.0f);
                float halo_size = 22.0f * camera_.zoom * pulse;
                fg_draw->AddCircle({p.x, p.y}, halo_size,
                                   IM_COL32(255, 255, 0, 80), 24, 3.0f);
                break;
            }
        }
    }

    // --- Render castle names ---
    if (show_names_) {
        for (const auto& cp : castles_) {
            if (!is_castle_visible(cp.index)) continue;

            auto p = map_to_screen(cp.x, cp.y);
            p.x += screen_cx;
            p.y += screen_cy - 14.0f;  // Offset above marker

            if (p.x < -150 || p.x > w + 150 || p.y < -150 || p.y > h + 150)
                continue;

            // Truncate long names
            std::string label = cp.name_utf8;
            if (label.size() > 14) label = label.substr(0, 14) + "..";

            // Add level indicator
            if (cp.level > 1) {
                label += fmt::format(" Lv.{}", cp.level);
            }

            ImU32 name_col = cp.selected
                ? IM_COL32(255, 220, 100, 255)
                : (cp.hovered ? IM_COL32(255, 255, 255, 240)
                              : IM_COL32(220, 220, 220, 220));
            fg_draw->AddText(static_cast<ImFont*>(nullptr), 12.0f,
                             ImVec2{p.x - 40.0f, p.y - 6.0f},
                             name_col, label.c_str());
        }
    }
}

void MapView::render_ui(Renderer& renderer) {
    int w = renderer.get_width();
    int h = renderer.get_height();

    // --- Hover tooltip ---
    if (auto hover = hovered_castle()) {
        const CastlePos* cp = nullptr;
        for (const auto& c : castles_) {
            if (c.index == *hover) { cp = &c; break; }
        }
        if (cp) {
            ImVec2 mouse = ImGui::GetMousePos();
            ImGui::SetNextWindowPos({mouse.x + 15, mouse.y + 15}, ImGuiCond_Always);
            ImGui::Begin("CastleTooltip", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoBackground);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s",
                               cp->name_utf8.empty() ? cp->name.c_str()
                                                     : cp->name_utf8.c_str());
            ImGui::Separator();
            ImGui::Text("Region: %u", cp->region_id);
            ImGui::Text("Level: %d", cp->level);
            ImGui::Text("Garrison: %d/%d", cp->garrison, cp->max_garrison);
            ImGui::Text("Gold: %d  Food: %d", cp->gold, cp->food);
            ImGui::Separator();
            ImGui::Text("Facilities:");
            ImGui::Text("  Wall: %s", cp->has_wall ? "Yes" : "No");
            ImGui::Text("  Barracks: %s", cp->has_barracks ? "Yes" : "No");
            ImGui::Text("  Storehouse: %s", cp->has_storehouse ? "Yes" : "No");
            ImGui::Text("  Market: %s", cp->has_market ? "Yes" : "No");
            ImGui::End();
        }
    }

    // --- Selected castle detail panel ---
    if (selected_) {
        const CastlePos* cp = nullptr;
        for (const auto& c : castles_) {
            if (c.index == *selected_) { cp = &c; break; }
        }
        if (cp) {
            ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({340, 420}, ImGuiCond_Always);
            ImGui::Begin("Castle Details", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s",
                               cp->name_utf8.empty() ? cp->name.c_str()
                                                     : cp->name_utf8.c_str());
            ImGui::Separator();
            ImGui::Text("Region: %u", cp->region_id);
            ImGui::Text("Level: %d", cp->level);
            ImGui::Text("Position: (%.0f, %.0f)", cp->x, cp->y);
            ImGui::Separator();
            ImGui::Text("Garrison: %d/%d", cp->garrison, cp->max_garrison);
            ImGui::Text("Gold: %d", cp->gold);
            ImGui::Text("Food: %d", cp->food);
            ImGui::Separator();
            ImGui::Text("Facilities:");
            ImGui::Text("  Wall: %s", cp->has_wall ? "Yes" : "No");
            ImGui::Text("  Barracks: %s", cp->has_barracks ? "Yes" : "No");
            ImGui::Text("  Storehouse: %s", cp->has_storehouse ? "Yes" : "No");
            ImGui::Text("  Market: %s", cp->has_market ? "Yes" : "No");
            ImGui::Separator();

            if (ImGui::Button("Castle Commands", {-1, 0})) {
                request_castle_command = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Start Battle", {-1, 0})) {
                request_battle = true;
            }

            // Campaign: Attack from this castle
            ImGui::Separator();
            ImGui::Text("Campaign:");
            if (cp->is_player_owned) {
                ImGui::Text("Owner: Player (Your castle)");
                auto attackable = get_attackable_enemy_castles(cp->index);
                if (!attackable.empty()) {
                    ImGui::Text("Attackable enemies: %zu", attackable.size());
                    static int attack_target_idx = 0;
                    if (attack_target_idx >= (int)attackable.size())
                        attack_target_idx = 0;
                    std::vector<const char*> target_names;
                    for (auto tid : attackable) {
                        const CastlePos* tc = nullptr;
                        for (const auto& c : castles_) {
                            if (c.index == tid) { tc = &c; break; }
                        }
                        target_names.push_back(tc
                            ? (tc->name_utf8.empty() ? tc->name.c_str()
                                                     : tc->name_utf8.c_str())
                            : "Unknown");
                    }
                    if (ImGui::Combo("##attack_target", &attack_target_idx,
                                     target_names.data(),
                                     (int)target_names.size())) {
                        attack_from_castle = cp->index;
                        attack_target_castle = attackable[attack_target_idx];
                    }
                    if (ImGui::Button("Launch Attack", {-1, 0})) {
                        if (attack_from_castle && attack_target_castle) {
                            request_battle = true;
                        }
                    }
                } else {
                    ImGui::TextDisabled("No adjacent enemy castles");
                }
            } else {
                ImGui::Text("Owner: %s",
                    cp->owner_id != 0 ? "Enemy" : "Neutral");
                if (can_attack_from_player_castle(cp->index)) {
                    if (ImGui::Button("Attack This Castle", {-1, 0})) {
                        // Find nearest player castle to attack from
                        for (const auto& pc : castles_) {
                            if (pc.is_player_owned && pc.region_id == cp->region_id) {
                                float dx = cp->x - pc.x;
                                float dy = cp->y - pc.y;
                                if (std::sqrt(dx*dx + dy*dy) < 25.0f) {
                                    attack_from_castle = pc.index;
                                    attack_target_castle = cp->index;
                                    request_battle = true;
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("Too far from your castles");
                }
            }

            ImGui::End();
        }
    }

    // --- Camera info ---
    ImGui::SetNextWindowPos({w - 200, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({190, 140}, ImGuiCond_Always);
    ImGui::Begin("Camera", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Zoom: %.2fx", camera_.zoom);
    ImGui::Text("Pos: (%.0f, %.0f)", camera_.x, camera_.y);
    ImGui::Separator();
    ImGui::Text("WASD/Arrows: Pan");
    ImGui::Text("Scroll: Zoom");
    ImGui::Text("Home: Reset");

    ImGui::Separator();
    ImGui::Text("Search Castles:");
    static char search_buf[64] = "";
    if (ImGui::InputText("##search", search_buf, IM_ARRAYSIZE(search_buf))) {
        search_filter_ = search_buf;
    }

    ImGui::Separator();
    ImGui::Text("Region Filter:");
    if (ImGui::Checkbox("All Regions", &show_all_regions_)) {
        if (show_all_regions_) active_region_ = 0;
    }
    if (!show_all_regions_) {
        ImGui::SameLine();
        ImGui::Text("Active: %u", active_region_);
        // Region selector
        static int region_sel = 0;
        if (ImGui::InputInt("##region", &region_sel)) {
            if (region_sel < 0) region_sel = 0;
            active_region_ = static_cast<uint32_t>(region_sel);
        }
    }

    ImGui::End();
}

bool MapView::handle_click(float mx, float my, Renderer& renderer) {
    auto map_pos = screen_to_map(mx, my);

    float best_dist = 999999.0f;
    std::optional<uint32_t> best_idx;

    for (const auto& cp : castles_) {
        if (!is_castle_visible(cp.index)) continue;
        float dx = cp.x - map_pos.x;
        float dy = cp.y - map_pos.y;
        float dist = dx * dx + dy * dy;
        float threshold = 12.0f / camera_.zoom;
        if (dist < threshold * threshold && dist < best_dist) {
            best_dist = dist;
            best_idx = cp.index;
        }
    }

    if (best_idx) {
        selected_ = best_idx;
        for (auto& cp : castles_) {
            cp.selected = (cp.index == *best_idx);
        }
        LOG_INFO("MapView: selected castle {} ('{}')", *best_idx,
                 castles_[*best_idx].name_utf8);
        return true;
    }

    selected_ = std::nullopt;
    for (auto& cp : castles_) cp.selected = false;
    return false;
}

bool MapView::is_castle_visible(uint32_t index) const {
    // Find the castle
    const CastlePos* cp = nullptr;
    for (const auto& c : castles_) {
        if (c.index == index) { cp = &c; break; }
    }
    if (!cp) return false;

    // Region filter
    if (!show_all_regions_ && cp->region_id != active_region_) return false;

    // Search filter
    if (!search_filter_.empty()) {
        std::string name_lower = cp->name_utf8.empty() ? cp->name : cp->name_utf8;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string filter_lower = search_filter_;
        std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (name_lower.find(filter_lower) == std::string::npos) return false;
    }

    return true;
}

void MapView::set_castle_owner(uint32_t castle_index, uint32_t owner_id,
                                bool is_player) {
    for (auto& cp : castles_) {
        if (cp.index == castle_index) {
            cp.owner_id = owner_id;
            cp.is_player_owned = is_player;
            break;
        }
    }
}

std::vector<uint32_t> MapView::get_player_castles() const {
    std::vector<uint32_t> result;
    for (const auto& cp : castles_) {
        if (cp.is_player_owned) {
            result.push_back(cp.index);
        }
    }
    return result;
}

std::vector<uint32_t> MapView::get_attackable_enemy_castles(
    uint32_t from_castle) const {
    std::vector<uint32_t> result;
    const CastlePos* from_cp = nullptr;
    for (const auto& cp : castles_) {
        if (cp.index == from_castle) { from_cp = &cp; break; }
    }
    if (!from_cp || !from_cp->is_player_owned) return result;

    for (const auto& cp : castles_) {
        if (!cp.is_player_owned && cp.owner_id != 0) {
            // Check if adjacent (within same region and close distance)
            if (cp.region_id == from_cp->region_id) {
                float dx = cp.x - from_cp->x;
                float dy = cp.y - from_cp->y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 25.0f) {  // Adjacent threshold
                    result.push_back(cp.index);
                }
            }
        }
    }
    return result;
}

bool MapView::can_attack_from_player_castle(uint32_t target_castle) const {
    const CastlePos* target_cp = nullptr;
    for (const auto& cp : castles_) {
        if (cp.index == target_castle) { target_cp = &cp; break; }
    }
    if (!target_cp || target_cp->is_player_owned) return false;

    for (const auto& cp : castles_) {
        if (cp.is_player_owned && cp.region_id == target_cp->region_id) {
            float dx = target_cp->x - cp.x;
            float dy = target_cp->y - cp.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 25.0f) return true;
        }
    }
    return false;
}

int32_t MapView::player_castle_count() const {
    int32_t count = 0;
    for (const auto& cp : castles_) {
        if (cp.is_player_owned) count++;
    }
    return count;
}

void MapView::plan_route(uint32_t from, uint32_t to) {
    route_from_ = from;
    route_to_ = to;
    planned_route_.clear();

    if (from == to) return;

    // Simple greedy path: connect castles by nearest neighbor within same region
    // Find start and end castle positions
    const CastlePos* start_cp = nullptr;
    const CastlePos* end_cp = nullptr;
    for (const auto& c : castles_) {
        if (c.index == from) start_cp = &c;
        if (c.index == to) end_cp = &c;
    }
    if (!start_cp || !end_cp) return;

    // Build path using greedy nearest-neighbor within region
    uint32_t current = from;
    std::set<uint32_t> visited;
    visited.insert(from);

    planned_route_.push_back({from, from});  // placeholder, will be replaced

    for (int step = 0; step < 20; ++step) {  // max 20 hops
        if (current == to) break;

        // Find nearest unvisited castle in same region
        const CastlePos* cur_cp = nullptr;
        for (const auto& c : castles_) {
            if (c.index == current) { cur_cp = &c; break; }
        }
        if (!cur_cp) break;

        uint32_t next = 0;
        float best_dist = 999999.0f;
        for (const auto& c : castles_) {
            if (visited.count(c.index)) continue;
            if (c.region_id != cur_cp->region_id) continue;
            if (c.index == current) continue;
            float dx = c.x - cur_cp->x;
            float dy = c.y - cur_cp->y;
            float dist = dx * dx + dy * dy;
            if (dist < best_dist) {
                best_dist = dist;
                next = c.index;
            }
        }

        if (next == 0) break;  // No more castles in region

        planned_route_.push_back({current, next});
        visited.insert(next);
        current = next;

        if (current == to) break;
    }

    // Replace first placeholder with actual first edge
    if (!planned_route_.empty() && planned_route_.front().first == from &&
        planned_route_.front().second == from) {
        planned_route_.erase(planned_route_.begin());
    }
}

void MapView::draw_planned_route(ImDrawList* draw, float screen_cx,
                                  float screen_cy) {
    if (planned_route_.empty()) return;

    ImU32 route_col = IM_COL32(255, 215, 0, 200);  // Gold for planned route

    for (const auto& [from, to] : planned_route_) {
        const CastlePos* ca = nullptr;
        const CastlePos* cb = nullptr;
        for (const auto& c : castles_) {
            if (c.index == from) ca = &c;
            if (c.index == to) cb = &c;
        }
        if (!ca || !cb) continue;

        auto pa = map_to_screen(ca->x, ca->y);
        auto pb = map_to_screen(cb->x, cb->y);
        pa.x += screen_cx; pa.y += screen_cy;
        pb.x += screen_cx; pb.y += screen_cy;

        draw->AddLine({pa.x, pa.y}, {pb.x, pb.y}, route_col, 3.0f);

        // Draw arrow at destination
        float dx = pb.x - pa.x;
        float dy = pb.y - pa.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0) {
            dx /= len;
            dy /= len;
            ImVec2 arrow_head{pb.x - dx * 8.0f, pb.y - dy * 8.0f};
            draw->AddLine({pb.x, pb.y}, arrow_head, route_col, 2.0f);
        }
    }
}

} // namespace tensyoki
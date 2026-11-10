/**
 * map_view.cpp - Strategic map view implementation
 *
 * Renders castles from sndata.dat on a 2D map with camera zoom/pan,
 * routes between castles, region boundaries, and click-to-select interaction.
 */

#include "map_view.hpp"
#include "core/logging.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <random>
#include <numeric>

namespace tensyoki {

MapView::MapView() = default;

bool MapView::init(const SndataArchive& sndata, GT1GTextureCache& tex_cache,
                   Renderer& renderer) {
    tex_cache_ = &tex_cache;
    renderer_ = &renderer;

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

    generate_positions();
    build_regions();
    build_routes();

    LOG_INFO("MapView: {} castles, {} routes, {} regions",
             castles_.size(), routes_.size(), regions_.size());
    return true;
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
        float zoom_ratio = camera_.zoom / old_zoom;
        // Adjust camera so zoom centers on mouse
        // (simplified: just center on current mouse)
        ImVec2 mouse = ImGui::GetMousePos();
        auto map_pos = screen_to_map(mouse.x, mouse.y);
        camera_.x = map_pos.x - (mouse.x - renderer.get_width() * 0.5f) / camera_.zoom;
        camera_.y = map_pos.y - (mouse.y - renderer.get_height() * 0.5f) / camera_.zoom;
    }
}

void MapView::update(float dt, Renderer& renderer) {
    handle_camera_input(renderer, dt);

    // Check for mouse wheel (ImGui handles this, but we check directly)
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
        float threshold = 10.0f / camera_.zoom;
        if (dist < threshold * threshold && dist < best_dist) {
            best_dist = dist;
            best_idx = cp.index;
        }
    }

    if (best_idx) {
        castles_[*best_idx].hovered = true;
    }
}

void MapView::render(Renderer& renderer) {
    if (castles_.empty()) return;

    int w = renderer.get_width();
    int h = renderer.get_height();
    float screen_cx = w * 0.5f;
    float screen_cy = h * 0.5f;

    // --- Render region backgrounds first ---
    if (show_regions_) {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        for (const auto& region : regions_) {
            if (region.castle_indices.size() < 2) continue;

            // Compute convex hull-ish center for the region
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

            // Draw a filled circle for the region
            ImVec4 col_vec{region.color.x, region.color.y, region.color.z, region.color.w};
            ImU32 col = ImGui::ColorConvertFloat4ToU32(col_vec);
            ImVec2 center_im{center.x, center.y};
            draw->AddCircleFilled(center_im, 60.0f * camera_.zoom, col, 12);
        }
    }

    // --- Render routes ---
    if (show_routes_) {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        ImU32 route_col = IM_COL32(100, 100, 100, 120);
        for (const auto& r : routes_) {
            const auto& ca = castles_[r.from];
            const auto& cb = castles_[r.to];
            auto pa = map_to_screen(ca.x, ca.y);
            auto pb = map_to_screen(cb.x, cb.y);
            pa.x += screen_cx; pa.y += screen_cy;
            pb.x += screen_cx; pb.y += screen_cy;
            ImVec2 pa_im{pa.x, pa.y};
            ImVec2 pb_im{pb.x, pb.y};
            draw->AddLine(pa_im, pb_im, route_col, 1.5f);
        }
    }

    // --- Render castle markers ---
    // Use a shared white texture for markers
    static std::shared_ptr<Renderer::Texture> white_tex;
    if (!white_tex) {
        Renderer::TextureCreateInfo ti{};
        ti.width = 1;
        ti.height = 1;
        ti.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
        uint8_t white[4] = {255, 255, 255, 255};
        ti.initial_data = white;
        ti.data_size = 4;
        white_tex = renderer.create_texture(ti);
    }

    // We need a SpriteRenderer to draw. For now use ImDrawList directly
    // for castle markers since SpriteRenderer setup is complex.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    for (auto& cp : castles_) {
        auto p = map_to_screen(cp.x, cp.y);
        p.x += screen_cx;
        p.y += screen_cy;

        // Skip if off-screen
        if (p.x < -50 || p.x > w + 50 || p.y < -50 || p.y > h + 50) continue;

        float size = 8.0f * camera_.zoom;
        ImVec2 center{p.x, p.y};
        ImU32 col = cp.selected
            ? IM_COL32(255, 200, 50, 255)   // selected: gold
            : (cp.hovered ? IM_COL32(255, 255, 255, 255)  // hovered: white
                          : IM_COL32(150, 150, 255, 255)); // normal: light blue

        draw->AddCircleFilled(center, size, col, 10);
        draw->AddCircle(center, size + 1.0f, IM_COL32(0, 0, 0, 150), 10, 1.5f);
    }

    // --- Render castle names ---
    if (show_names_) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        for (const auto& cp : castles_) {
            auto p = map_to_screen(cp.x, cp.y);
            p.x += screen_cx;
            p.y += screen_cy - 12.0f;  // Offset above marker

            if (p.x < -100 || p.x > w + 100 || p.y < -100 || p.y > h + 100)
                continue;

            // Truncate long names
            std::string label = cp.name_utf8;
            if (label.size() > 12) label = label.substr(0, 12) + "..";

            draw->AddText(nullptr, 12.0f, ImVec2{p.x - 30.0f, p.y - 6.0f},
                          IM_COL32(255, 255, 255, 230), label.c_str());
        }
    }

    // --- Render selected castle highlight ---
    if (selected_) {
        for (const auto& cp : castles_) {
            if (cp.index == *selected_) {
                auto p = map_to_screen(cp.x, cp.y);
                p.x += screen_cx;
                p.y += screen_cy;
                draw->AddCircle({p.x, p.y}, 18.0f * camera_.zoom,
                                IM_COL32(255, 255, 0, 100), 20, 3.0f);
                break;
            }
        }
    }
}

bool MapView::handle_click(float mx, float my, Renderer& renderer) {
    auto map_pos = screen_to_map(mx, my);

    float best_dist = 999999.0f;
    std::optional<uint32_t> best_idx;

    for (const auto& cp : castles_) {
        float dx = cp.x - map_pos.x;
        float dy = cp.y - map_pos.y;
        float dist = dx * dx + dy * dy;
        float threshold = 10.0f / camera_.zoom;
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

} // namespace tensyoki

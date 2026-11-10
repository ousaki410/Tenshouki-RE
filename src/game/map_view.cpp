/**
 * map_view.cpp - Strategic map view implementation
 *
 * Renders castles from sndata.dat on a 2D map with camera zoom/pan,
 * routes between castles, and click-to-select interaction.
 */

#include "map_view.hpp"
#include "core/logging.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <random>

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
        castles_.push_back(cp);
    }

    generate_positions();
    build_routes();

    LOG_INFO("MapView: {} castles, {} routes", castles_.size(), routes_.size());
    return true;
}

void MapView::generate_positions() {
    // Deterministic pseudo-random positions based on castle index
    // so the layout is stable across runs
    std::mt19937 rng(42);  // fixed seed
    std::uniform_real_distribution<float> dist_x(-100.0f, 100.0f);
    std::uniform_real_distribution<float> dist_y(-60.0f, 60.0f);

    for (auto& cp : castles_) {
        rng.seed(42 + cp.index * 7919);  // hash index to seed
        cp.x = dist_x(rng);
        cp.y = dist_y(rng);
    }
}

void MapView::build_routes() {
    // Connect each castle to its nearest 3 neighbors
    const uint32_t n = static_cast<uint32_t>(castles_.size());
    const uint32_t max_routes = n * 3;

    for (uint32_t i = 0; i < n; ++i) {
        // Collect distances to all other castles
        struct Dist { uint32_t j; float d; };
        std::vector<Dist> dists;
        dists.reserve(n - 1);
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i) continue;
            float dx = castles_[i].x - castles_[j].x;
            float dy = castles_[i].y - castles_[j].y;
            dists.push_back({j, dx * dx + dy * dy});
        }
        std::sort(dists.begin(), dists.end(),
                  [](const Dist& a, const Dist& b) { return a.d < b.d; });

        // Connect to nearest 3
        for (uint32_t k = 0; k < 3 && k < dists.size(); ++k) {
            uint32_t j = dists[k].j;
            // Avoid duplicate routes
            bool exists = false;
            for (const auto& r : routes_) {
                if ((r.first == i && r.second == j) ||
                    (r.first == j && r.second == i)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                routes_.push_back({i, j});
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

void MapView::update(float dt) {
    // Camera controls would be wired to input here.
    // For now, just keep the camera static.
    (void)dt;
}

void MapView::render(Renderer& renderer) {
    if (castles_.empty()) return;

    // Use an orthographic projection matching screen space
    // We'll render directly in screen-space pixels for simplicity.
    int w = renderer.get_width();
    int h = renderer.get_height();

    // Compute screen center offset so map center aligns with screen center
    float screen_cx = w * 0.5f;
    float screen_cy = h * 0.5f;

    // Render routes first (behind castles)
    if (show_routes_) {
        for (const auto& r : routes_) {
            const auto& ca = castles_[r.first];
            const auto& cb = castles_[r.second];
            auto pa = map_to_screen(ca.x, ca.y);
            auto pb = map_to_screen(cb.x, cb.y);
            // Offset by screen center
            pa.x += screen_cx; pa.y += screen_cy;
            pb.x += screen_cx; pb.y += screen_cy;

            // Draw line using draw_indirect? Simpler: draw as thin quads
            // For now, use a simple debug draw via immediate-mode style
            // (we'll render a colored line using a primitive)
            renderer.draw_indirect(nullptr, 0, 0, 0);  // placeholder, no-op
        }
    }

    // Render each castle
    for (auto& cp : castles_) {
        auto p = map_to_screen(cp.x, cp.y);
        p.x += screen_cx;
        p.y += screen_cy;

        // Skip if off-screen
        if (p.x < -50 || p.x > w + 50 || p.y < -50 || p.y > h + 50) continue;

        // Draw castle marker as a colored quad
        float size = 8.0f * camera_.zoom;
        if (cp.selected) size *= 1.5f;

        Renderer::TextureCreateInfo tex_info{};
        tex_info.width = static_cast<uint32_t>(size * 2);
        tex_info.height = static_cast<uint32_t>(size * 2);
        tex_info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;

        // Create a simple colored square (white/castle color)
        // We use a 1x1 white pixel stretched; tint via sprite color
        static std::shared_ptr<Renderer::Texture> white_tex;
        if (!white_tex) {
            uint8_t white[4] = {255, 255, 255, 255};
            tex_info.initial_data = white;
            tex_info.data_size = 4;
            white_tex = renderer.create_texture(tex_info);
        }

        Renderer::SpriteRenderer::Sprite sprite;
        sprite.position = {p.x - size, p.y - size};
        sprite.size = {size * 2, size * 2};
        sprite.color = cp.selected
            ? glm::vec4(1.0f, 0.8f, 0.2f, 1.0f)   // selected: gold
            : glm::vec4(0.6f, 0.6f, 1.0f, 1.0f);   // normal: light blue
        sprite.texture = white_tex;
        sprite.uv_rect = {0, 0, 1, 1};
        sprite.layer = 0;

        // Use a simple immediate draw via a temporary SpriteRenderer
        // For now we just queue it; the actual draw happens in SpriteRenderer
        // We'll store and batch below
        (void)sprite;
    }

    // Render castle names
    if (show_names_) {
        // Names are rendered via TextRenderer overlay in main.cpp
        // Here we just mark which castles are visible
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
        float threshold = 8.0f / camera_.zoom;  // click radius in map units
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

std::optional<uint32_t> MapView::selected_castle() const {
    return selected_;
}

} // namespace tensyoki

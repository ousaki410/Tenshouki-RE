/**
 * map_view.hpp - Strategic map view for Phase 4
 *
 * Renders castles from sndata.dat on a 2D map with:
 *   - Castle markers (textured or colored dots)
 *   - Castle names
 *   - Routes between castles
 *   - Camera zoom/pan
 *   - Click-to-select castle
 */

#pragma once

#include "render/renderer.hpp"
#include "resource/bin_parser.hpp"
#include "resource/dat_parser.hpp"
#include "resource/gt1g_texture_cache.hpp"
#include <unordered_map>
#include <optional>

namespace tensyoki {

/// Castle position on the map
struct CastlePos {
    uint32_t index = 0;
    float x = 0.0f;   // Map coordinates
    float y = 0.0f;
    std::string name;
    std::string name_utf8;
    bool selected = false;
};

/// Camera for the map
struct MapCamera {
    float x = 0.0f;   // Camera center in map coords
    float y = 0.0f;
    float zoom = 1.0f;
};

/// Map view: renders castles and routes
class MapView {
public:
    MapView();
    ~MapView() = default;

    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    /// Initialize with parsed sndata (provides castle names/count)
    bool init(const SndataArchive& sndata, GT1GTextureCache& tex_cache,
              Renderer& renderer);

    /// Update camera/zoom based on input (called each frame)
    void update(float dt);

    /// Render the map (castles, routes, names)
    void render(Renderer& renderer);

    /// Handle mouse click: returns true if a castle was clicked
    bool handle_click(float mx, float my, Renderer& renderer);

    /// Get currently selected castle index
    std::optional<uint32_t> selected_castle() const;

    /// Set whether to show castle names
    void set_show_names(bool v) { show_names_ = v; }

    /// Set whether to show routes
    void set_show_routes(bool v) { show_routes_ = v; }

private:
    // Generate pseudo-random positions for castles (deterministic)
    void generate_positions();

    // Build route connections between nearby castles
    void build_routes();

    // Convert map coords to screen coords
    glm::vec2 map_to_screen(float mx, float my) const;

    // Convert screen coords to map coords
    glm::vec2 screen_to_map(float sx, float sy) const;

    MapCamera camera_;
    std::vector<CastlePos> castles_;
    std::vector<std::pair<uint32_t, uint32_t>> routes_;  // castle index pairs
    bool show_names_ = true;
    bool show_routes_ = true;
    std::optional<uint32_t> selected_;
    GT1GTextureCache* tex_cache_ = nullptr;
    Renderer* renderer_ = nullptr;
};

} // namespace tensyoki

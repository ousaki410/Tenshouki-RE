/**
 * map_view.hpp - Strategic map view for Phase 4
 *
 * Renders castles from sndata.dat on a 2D map with:
 *   - Castle markers (colored dots with optional portraits)
 *   - Castle names with UTF-8/CJK support
 *   - Routes between nearby castles
 *   - Region boundaries (clusters of castles)
 *   - Camera zoom/pan with keyboard/mouse
 *   - Click-to-select castle with detail popup
 *   - Date/season overlay
 */

#pragma once

#include "render/renderer.hpp"
#include "resource/bin_parser.hpp"
#include "resource/dat_parser.hpp"
#include "resource/gt1g_texture_cache.hpp"
#include "game/game_date.hpp"
#include "castle_command.hpp"
#include <imgui.h>
#include <unordered_map>
#include <optional>

namespace tensyoki {

/// Castle position and state on the map
struct CastlePos {
    uint32_t index = 0;
    float x = 0.0f;   // Map coordinates
    float y = 0.0f;
    std::string name;
    std::string name_utf8;
    bool selected = false;
    bool hovered = false;
    uint32_t region_id = 0;  // Which region this castle belongs to
    // Facility flags from castle_command
    bool has_wall = false;
    bool has_barracks = false;
    bool has_storehouse = false;
    bool has_market = false;
    int32_t level = 1;
    int32_t garrison = 0;
    int32_t max_garrison = 0;
    int32_t gold = 0;
    int32_t food = 0;
    // Sprite texture for this castle (loaded from GT1G)
    std::shared_ptr<Renderer::Texture> sprite_tex;
    bool sprite_loaded = false;
};

/// Camera for the map
struct MapCamera {
    float x = 0.0f;   // Camera center in map coords
    float y = 0.0f;
    float zoom = 1.0f;
};

/// Route between two castles
struct MapRoute {
    uint32_t from = 0;
    uint32_t to = 0;
    float distance = 0.0f;
};

/// Region (province) on the map
struct MapRegion {
    uint32_t id = 0;
    std::string name;
    glm::vec4 color = {0.5f, 0.5f, 0.5f, 0.3f};  // Fill color
    std::vector<uint32_t> castle_indices;
};

/// Map view: renders castles, routes, regions, and UI
class MapView {
public:
    MapView();
    ~MapView() = default;

    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    /// Initialize with parsed sndata (provides castle names/count)
    bool init(const SndataArchive& sndata, GT1GTextureCache& tex_cache,
              Renderer& renderer, const BinArchive* grp_archive = nullptr);

    /// Update castle data from castle_command (facilities, garrison, etc.)
    void update_castle_data(const CastleCommand& cmd);

    /// Update camera/zoom based on input (called each frame)
    void update(float dt, Renderer& renderer);

    /// Render the map (castles, routes, regions, names)
    void render(Renderer& renderer);

    /// Render UI overlay (castle details panel, tooltips)
    void render_ui(Renderer& renderer);

    /// Handle mouse click: returns true if a castle was clicked
    bool handle_click(float mx, float my, Renderer& renderer);

    /// Get currently selected castle index
    std::optional<uint32_t> selected_castle() const { return selected_; }

    /// Set whether to show castle names
    void set_show_names(bool v) { show_names_ = v; }

    /// Set whether to show routes
    void set_show_routes(bool v) { show_routes_ = v; }

    /// Set whether to show region boundaries
    void set_show_regions(bool v) { show_regions_ = v; }

    /// Set search filter for castle names (empty = show all)
    void set_search_filter(const std::string& filter) { search_filter_ = filter; }

    /// Set active region filter (0 = all regions)
    void set_active_region(uint32_t region_id) { active_region_ = region_id; }

    /// Set whether to show all regions or only active one
    void set_show_all_regions(bool v) { show_all_regions_ = v; }

    /// Get reference to castles for external access
    const std::vector<CastlePos>& castles() const { return castles_; }

    /// Get reference to regions
    const std::vector<MapRegion>& regions() const { return regions_; }

    /// Get hovered castle index (for tooltips)
    std::optional<uint32_t> hovered_castle() const {
        for (const auto& c : castles_) {
            if (c.hovered) return c.index;
        }
        return std::nullopt;
    }

    /// Get route planning endpoints
    std::optional<uint32_t> route_from() const { return route_from_; }
    std::optional<uint32_t> route_to() const { return route_to_; }

    /// Plan a route between two castles
    void plan_route(uint32_t from, uint32_t to);

    /// Check if a castle passes all visibility filters
    bool is_castle_visible(uint32_t index) const;

    /// Request flags set by UI buttons, checked by MapViewState
    bool request_castle_command = false;
    bool request_battle = false;

    /// Clear request flags (call after processing in MapViewState)
    void clear_requests() {
        request_castle_command = false;
        request_battle = false;
    }

private:
    // Generate deterministic positions based on castle index (regional clusters)
    void generate_positions();

    // Build route connections between nearby castles (within same region)
    void build_routes();

    // Group castles into regions based on position proximity
    void build_regions();

    // Convert map coords to screen coords
    glm::vec2 map_to_screen(float mx, float my) const;

    // Convert screen coords to map coords
    glm::vec2 screen_to_map(float sx, float sy) const;

    // Handle camera movement from keyboard
    void handle_camera_input(Renderer& renderer, float dt);

    // Handle mouse wheel zoom
    void handle_mouse_wheel(Renderer& renderer, float y_offset);

    // Load a GT1G portrait texture for a castle (by index)
    void load_castle_sprite(uint32_t castle_index);

    // Draw a castle sprite marker
    void draw_castle_sprite(ImDrawList* draw, const CastlePos& cp,
                            float screen_x, float screen_y, float size);

    // Draw route with distance label
    void draw_route(ImDrawList* draw, const MapRoute& route,
                    float screen_cx, float screen_cy);

    // Draw planned route path (sequence of castles)
    void draw_planned_route(ImDrawList* draw, float screen_cx, float screen_cy);

    MapCamera camera_;
    std::vector<CastlePos> castles_;
    std::vector<MapRoute> routes_;
    std::vector<MapRegion> regions_;
    bool show_names_ = true;
    bool show_routes_ = true;
    bool show_regions_ = true;
    bool show_all_regions_ = true;
    uint32_t active_region_ = 0;
    std::string search_filter_;
    std::optional<uint32_t> selected_;
    std::optional<uint32_t> route_from_;
    std::optional<uint32_t> route_to_;
    std::vector<std::pair<uint32_t, uint32_t>> planned_route_;
    GT1GTextureCache* tex_cache_ = nullptr;
    const BinArchive* grp_archive_ = nullptr;
    Renderer* renderer_ = nullptr;
    // Shared white pixel texture for fallback markers
    std::shared_ptr<Renderer::Texture> white_tex_;
};

} // namespace tensyoki

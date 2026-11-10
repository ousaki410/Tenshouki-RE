/**
 * game_context.hpp - Shared game resources for Phase 4
 *
 * Holds all resources that game states need:
 *   - Renderer (OpenGL)
 *   - TextRenderer (FreeType)
 *   - SndataArchive (officers/castles)
 *   - GT1GTextureCache (portrait textures)
 *   - ShaderCompiler (shader compilation)
 *   - GameDate (in-game date)
 */

#pragma once

#include "game_date.hpp"
#include "render/renderer.hpp"
#include "resource/bin_parser.hpp"
#include "resource/dat_parser.hpp"
#include "resource/gt1g_texture_cache.hpp"
#include "render/shader_compiler.hpp"
#include "castle_command.hpp"
#include "save_system.hpp"
#include <memory>

namespace tensyoki {

class GameContext {
public:
    GameContext() = default;
    ~GameContext() = default;

    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;

    // Resources (set by Application during init)
    Renderer* renderer = nullptr;
    std::unique_ptr<Renderer::TextRenderer> text_renderer;
    std::unique_ptr<ShaderCompiler> shader_compiler;
    std::unique_ptr<BinArchive> grp_archive;
    std::unique_ptr<SndataArchive> sndata;
    GT1GTextureCache* tex_cache = nullptr;  // not owned, points to Application's cache
    GameDate date;
    GameClock clock;
    CastleCommand castle_command;

    // State transition requests (set by states, processed by Application)
    enum class PendingState {
        None,
        MainMenu,
        MapView,
        OfficerDetail,
        CastleCommand,
        Battle,
    };
    PendingState pending_push = PendingState::None;
    bool request_pop = false;
    bool request_clear = false;
    
    // Selected castle for commands
    uint32_t selected_castle_id = 0;
    
    // Target castle for battle (map-to-battle linkage)
    std::optional<uint32_t> battle_target_castle;

    // Save system
    SaveSystem save_system;
    std::string save_dir = "./saves";
};

} // namespace tensyoki

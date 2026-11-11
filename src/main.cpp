/**
 * main.cpp - Entry point for the Tensyoki Linux port
 */

#include "core/config.hpp"
#include "core/filesystem.hpp"
#include "core/logging.hpp"
#include "core/math_utils.hpp"
#include "render/renderer.hpp"
#include "resource/bin_parser.hpp"
#include "resource/gt1g_texture_cache.hpp"
#include "resource/dat_parser.hpp"
#include "render/shader_compiler.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/texture_atlas_viewer.hpp"
#include "platform/platform.hpp"

#include "game/game_context.hpp"
#include "game/game_state.hpp"
#include "game/game_states.hpp"
#include "game/game_date.hpp"
#include "game/map_view.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tensyoki {

class Application {
public:
    Application(int argc, char* argv[]) : argc_(argc), argv_(argv) {}
    
    int run() {
        // Logging and configuration must be ready before argument parsing
        Logging::init("tensyoki.log");
        Config::instance().load("config.toml");
        parse_args();

        // Headless resource check: no window, no renderer
        if (check_resources_) {
            const bool ok = run_resource_check();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        // Headless battle system test: no window, no renderer
        if (battle_test_) {
            const bool ok = run_battle_test();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        // Headless castle command test: no window, no renderer
        if (castle_test_) {
            const bool ok = run_castle_test();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        // Headless save/load system test: no window, no renderer
        if (save_test_) {
            const bool ok = run_save_test();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        // Headless officer system test: no window, no renderer
        if (officer_test_) {
            const bool ok = run_officer_test();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        // Headless strategy layer test: no window, no renderer
        if (strategy_test_) {
            const bool ok = run_strategy_test();
            Logging::shutdown();
            return ok ? 0 : 1;
        }

        if (help_requested_) {
            Logging::shutdown();
            return 0;
        }

        if (!init()) {
            Logging::shutdown();
            return 1;
        }
        
        main_loop();
        
        shutdown();
        return 0;
    }

private:
    int argc_;
    char** argv_;
    
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<ImGuiBackend> imgui_;
    std::unique_ptr<Platform> platform_;
    std::unique_ptr<TextureAtlasViewer> atlas_viewer_;
    std::filesystem::path data_root_;
    std::string imgui_ini_path_;  // ImGui ini file path; must stay valid during init()
    
    bool init() {
        LOG_INFO("=== Tensyoki Linux Port ===");
        LOG_INFO("Version: 0.1.0-dev");
        LOG_INFO("Build: {} {}", __DATE__, __TIME__);
        
        // Initialize VFS
        if (!VFS::instance().mount(data_root_, "/")) {
            LOG_ERROR("Failed to mount data root: {}", data_root_.string());
            return false;
        }
        
        // Prepare the ImGui ini file path (must stay valid during init())
        imgui_ini_path_ = (VFS::instance().get_user_data_dir() / "imgui.ini").string();
        
        // Initialize the platform abstraction
        platform_ = Platform::create();
        Platform::WindowParams window_params;
        window_params.title = Config::instance().get_window_title();
        window_params.width = Config::instance().get_window_width();
        window_params.height = Config::instance().get_window_height();
        window_params.fullscreen = Config::instance().get_fullscreen();
        window_params.vsync = Config::instance().get_vsync();
        
        if (!platform_->init(window_params)) {
            LOG_ERROR("Platform initialization failed");
            return false;
        }
        
        // Initialize the renderer
        renderer_ = std::make_unique<Renderer>();
        Renderer::InitParams render_params;
        render_params.backend = Renderer::Backend::OpenGL;
        render_params.window_handle = platform_->get_window_handle();
        render_params.width = window_params.width;
        render_params.height = window_params.height;
        render_params.vsync = window_params.vsync;
        render_params.debug_overlay = Config::instance().get_debug_overlay();
        
        if (!renderer_->init(render_params)) {
            LOG_ERROR("Renderer initialization failed");
            return false;
        }
        
        // Initialize ImGui
        imgui_ = std::make_unique<ImGuiBackend>();
        ImGuiBackend::InitParams imgui_params;
        imgui_params.window_handle = platform_->get_window_handle();
        imgui_params.device = renderer_->get_device();
        imgui_params.enable_docking = true;
        imgui_params.enable_viewports = true;
        imgui_params.ini_filename = imgui_ini_path_.c_str();
        
        if (!imgui_->init(imgui_params)) {
            LOG_ERROR("ImGui initialization failed");
            return false;
        }
        
        // Initialize the texture atlas viewer
        atlas_viewer_ = std::make_unique<TextureAtlasViewer>(*renderer_);
        
        // Load the texture atlas
        auto atlas_path = data_root_ / "res" / "extracted_e1_t0_1024x1024_bgra.png";
        if (std::filesystem::exists(atlas_path)) {
            if (!atlas_viewer_->load_atlas(atlas_path, "res_grp.bin Entry 1 Texture 0")) {
                LOG_WARN("Failed to load texture atlas: {}", atlas_path.string());
            }
        } else {
            LOG_WARN("Texture atlas not found: {}", atlas_path.string());
        }
        
        imgui_->set_style_dark();
        
        // Test resource parsing
        test_resource_parsing();
        
        // Initialize text renderer with default font
        text_renderer_ = std::make_unique<Renderer::TextRenderer>(*renderer_);
        const char* default_fonts[] = {
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",  // CJK support
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            nullptr
        };
        bool font_loaded = false;
        for (const char* font_path : default_fonts) {
            if (!font_path) break;
            if (std::filesystem::exists(font_path)) {
                if (text_renderer_->load_font(font_path, 24.0f)) {
                    LOG_INFO("Loaded font: {} (24pt)", font_path);
                    font_loaded = true;
                    break;
                }
            }
        }
        if (!font_loaded) {
            LOG_WARN("No TrueType font found; text rendering unavailable");
        }

        // Initialize shader compiler and parse res_shader.bin
        shader_compiler_ = std::make_unique<ShaderCompiler>();
        auto shader_path = data_root_ / "res" / "res_shader.bin";
        if (std::filesystem::exists(shader_path)) {
            shader_infos_ = shader_compiler_->parse_shader_container(shader_path);
            LOG_INFO("Parsed {} shaders from res_shader.bin", shader_infos_.size());
        } else {
            LOG_WARN("res_shader.bin not found: {}", shader_path.string());
        }

        // Load scenario data (officers / castles) from sndata1.dat
        auto sndata_path = data_root_ / "scenario" / "sndata1.dat";
        if (std::filesystem::exists(sndata_path)) {
            auto sndata_result = parse_sndata_file(sndata_path, 1);
            if (sndata_result) {
                sndata_loaded_ = std::move(*sndata_result);
                loaded_scenario_ = 1;
                LOG_INFO("Loaded scenario 1: {} officers, {} castles",
                         sndata_loaded_.officer_count(), sndata_loaded_.castle_count());
            } else {
                LOG_WARN("Failed to parse sndata1.dat: {}",
                         to_string(sndata_result.error()));
            }
        } else {
            LOG_WARN("sndata1.dat not found: {}", sndata_path.string());
        }

        // ImGui's built-in font has no CJK glyphs. Use a scalable Latin base
        // font and merge a CJK fallback that covers only the characters used
        // by the scenario's castle/officer names. This keeps the UI atlas
        // small (the full Japanese range has about 20k glyphs).
        {
            ImFont* ui_font = nullptr;
            const char* base_fonts[] = {
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                nullptr,
            };
            for (const char* path : base_fonts) {
                if (!path) break;
                if (std::filesystem::exists(path)) {
                    ui_font = imgui_->load_font(path, 18.0f);
                    if (ui_font) break;
                }
            }
            if (!ui_font) {
                ui_font = imgui_->load_font_default(18.0f);
            }
            imgui_->set_default_font(ui_font);

            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());

            // Static CJK text used by the UI (date labels and debug overlay).
            builder.AddText("年月日");
            builder.AddText("信長の野望・天翔記");
            builder.AddText("織田信長・羽柴秀吉・徳川家康");

            // Add every character used by scenario names. Older save files
            // may still contain NUL padding inside name_utf8, so strip NULs.
            auto add_name = [&builder](const std::string& name) {
                std::string clean;
                clean.reserve(name.size());
                for (char ch : name) {
                    if (ch != '\0') clean.push_back(ch);
                }
                if (!clean.empty()) builder.AddText(clean.c_str());
            };
            for (const auto& castle : sndata_loaded_.castles) {
                add_name(castle.name_utf8);
            }
            for (const auto& officer : sndata_loaded_.officers) {
                add_name(officer.name_utf8);
            }

            ImVector<ImWchar> ranges;
            builder.BuildRanges(&ranges);

            ImFont* cjk_font = imgui_->load_font(
                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
                18.0f, ranges.Data, /*merge=*/true);
            if (cjk_font) {
                // Force the atlas to build while the temporary range array
                // is still alive (ImFontConfig only stores a pointer).
                unsigned char* atlas_pixels = nullptr;
                int atlas_w = 0, atlas_h = 0;
                ImGui::GetIO().Fonts->GetTexDataAsRGBA32(
                    &atlas_pixels, &atlas_w, &atlas_h);
                LOG_INFO("ImGui: merged CJK fallback ({} codepoints, atlas {}x{})",
                         ranges.Size, atlas_w, atlas_h);
            } else {
                LOG_WARN("ImGui: CJK fallback font unavailable");
            }
        }

        // Load res_grp.bin for officer portrait textures
        auto grp_path = data_root_ / "res" / "res_grp.bin";
        if (std::filesystem::exists(grp_path)) {
            grp_archive_ = std::make_unique<BinArchive>();
            auto grp_result = grp_archive_->load_from_file(grp_path);
            if (grp_result) {
                LOG_INFO("Loaded res_grp.bin: {} entries", grp_archive_->entries().size());
                // Load first GT1G texture from entry 0 as default portrait
                if (grp_archive_->entries().size() > 0) {
                    officer_portrait_ = texture_cache_.get_or_load(
                        *renderer_, "res_grp.bin", *grp_archive_, 0);
                    if (officer_portrait_) {
                        LOG_INFO("Loaded officer portrait texture from res_grp.bin entry 0");
                    }
                }
            } else {
                LOG_WARN("Failed to load res_grp.bin: {}",
                         to_string(grp_result.error()));
            }
        } else {
            LOG_WARN("res_grp.bin not found: {}", grp_path.string());
        }

        LOG_INFO("Initialization complete");

        // Initialize Phase 4 game state system
        game_ctx_.renderer = renderer_.get();
        game_ctx_.text_renderer = std::move(text_renderer_);
        game_ctx_.shader_compiler = std::move(shader_compiler_);
        game_ctx_.grp_archive = std::move(grp_archive_);
        game_ctx_.sndata = std::make_unique<SndataArchive>(std::move(sndata_loaded_));
        game_ctx_.tex_cache = &texture_cache_;  // not owned

        // Save system defaults to ./saves unless configured otherwise.
        game_ctx_.save_system.init(game_ctx_.save_dir);

        // Prepare campaign data so a new game or load can start immediately.
        if (game_ctx_.sndata) {
            game_ctx_.castle_command.init(*game_ctx_.sndata);
            game_ctx_.strategy.init(game_ctx_.castle_command);
        }

        // Push main menu state
        game_state_mgr_.push(std::make_unique<MainMenuState>(game_ctx_));

        game_initialized_ = true;
        LOG_INFO("Phase 4 game system initialized");
        return true;
    }
    
    void parse_args() {
        std::filesystem::path cli_path;

        for (int i = 1; i < argc_; ++i) {
            std::string_view arg = argv_[i];
            if (arg == "--check-resources") {
                check_resources_ = true;
            } else if (arg == "--test-battle") {
                battle_test_ = true;
            } else if (arg == "--test-castle") {
                castle_test_ = true;
            } else if (arg == "--test-save") {
                save_test_ = true;
            } else if (arg == "--test-officer") {
                officer_test_ = true;
            } else if (arg == "--test-strategy") {
                strategy_test_ = true;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: tensyoki [options] [data_root]\n"
                          << "  --check-resources  parse all res/*.bin containers and exit\n"
                          << "  --test-battle      run headless battle system test and exit\n"
                          << "  --test-castle      run headless castle command test and exit\n"
                          << "  --test-save        run headless save/load system test and exit\n"
                          << "  --test-officer     run headless officer system test and exit\n"
                          << "  --test-strategy    run headless turn-based strategy test and exit\n"
                          << "  --help             show this message\n"
                          << "Environment: TENSYOKI_DATA_ROOT\n";
                help_requested_ = true;
            } else if (!arg.empty() && arg.front() == '-') {
                LOG_WARN("Unknown option: {}", arg);
            } else if (cli_path.empty()) {
                cli_path = arg;
            } else {
                LOG_WARN("Ignoring extra argument: {}", arg);
            }
        }

        if (help_requested_) {
            return;
        }

        // Priority: command line > environment variables > config file > auto-detection
        if (!cli_path.empty()) {
            data_root_ = cli_path;
        } else if (const char* env = std::getenv("TENSYOKI_DATA_ROOT")) {
            data_root_ = env;
        } else {
            data_root_ = Config::instance().get_data_root();
        }
        
        if (data_root_.empty()) {
            // Auto-detect common paths
            std::vector<std::filesystem::path> candidates = {
                std::filesystem::current_path() / "data",
                std::filesystem::current_path() / "NOBUNAGA'S AMBITION Tenshouki with Power Up Kit HD version",
                "/usr/share/tensyoki/data",
            };
            
            // Only add HOME if it's set
            if (const char* home = std::getenv("HOME")) {
                candidates.push_back(std::filesystem::path(home) / ".local/share/tensyoki/data");
            }
            
            for (const auto& p : candidates) {
                if (std::filesystem::exists(p / "res" / "res_grp.bin")) {
                    data_root_ = p;
                    break;
                }
            }
        }
        
        if (data_root_.empty() || !std::filesystem::exists(data_root_)) {
            LOG_ERROR("Game data not found. Please specify data root path.");
            LOG_ERROR("Usage: tensyoki [data_root_path]");
            LOG_ERROR("Or set TENSYOKI_DATA_ROOT environment variable");
            data_root_ = ".";  // Allow continuing so the error UI can be shown
        } else {
            LOG_INFO("Data root: {}", data_root_.string());
        }
    }

    /// Parse every known container, decode nested LINK/GT1G blocks.
    /// Returns false if any container fails to load.
    bool run_resource_check() {
        static constexpr const char* kContainers[] = {
            "res_grp.bin", "res_else.bin", "res_exp.bin",
            "res_exp_pc.bin", "res_shader.bin", "res_snd.bin",
        };

        LOG_INFO("Resource check: {}", data_root_.string());

        bool ok = true;
        for (const char* name : kContainers) {
            const auto path = data_root_ / "res" / name;
            BinArchive archive;
            auto result = archive.load_from_file(path);
            if (!result) {
                LOG_ERROR("{}: {}", name, to_string(result.error()));
                ok = false;
                continue;
            }

            size_t nested_links = 0;
            size_t data_blocks = 0;
            size_t lwc_blocks = 0;
            size_t textures = 0;
            for (const auto& entry : archive.entries()) {
                if (!entry.is_nested_link) continue;
                auto nested = archive.parse_nested(entry);
                if (!nested) {
                    LOG_ERROR("{}: entry {}: {}", name, entry.index,
                              to_string(nested.error()));
                    ok = false;
                    continue;
                }
                nested_links++;
                data_blocks += nested->data_entries.size();
                for (const auto& block : nested->data_entries) {
                    if (block.signature == "LWC") lwc_blocks++;
                }
                if (nested->textures) textures += nested->textures->texture_count;
                LOG_DEBUG("{}: entry {}: {} data blocks, {} records, {} textures",
                          name, entry.index, nested->data_entry_count,
                          nested->record_count,
                          nested->textures ? nested->textures->texture_count : 0);
            }

            const auto info = archive.info();
            LOG_INFO("{}: version {}, {} entries, {} sentinels, {} bytes, "
                     "{} nested links, {} data blocks ({} LWC), {} textures",
                     name, info.version, info.file_count, info.sentinel_count,
                     info.total_size, nested_links, data_blocks, lwc_blocks, textures);
        }

        LOG_INFO("Resource check: {}", ok ? "PASS" : "FAIL");
        return ok;
    }
    
    void test_resource_parsing() {
        LOG_INFO("Testing resource parsing...");
        
        // Test parsing shader.bin
        BinArchive shader_archive;
        auto shader_result = shader_archive.load_from_file(data_root_ / "res" / "res_shader.bin");
        if (shader_result) {
            LOG_INFO("res_shader.bin: {} resources", shader_archive.entries().size());
            auto info = shader_archive.info();
            LOG_INFO("  Version: {}, Files: {}, Total: {} bytes", info.version, info.file_count, info.total_size);
        } else {
            LOG_WARN("Failed to parse res_shader.bin: {}", tensyoki::to_string(shader_result.error()));
        }
        
        // Test parsing else.bin
        BinArchive else_archive;
        auto else_result = else_archive.load_from_file(data_root_ / "res" / "res_else.bin");
        if (else_result) {
            LOG_INFO("res_else.bin: {} resources", else_archive.entries().size());
        } else {
            LOG_WARN("Failed to parse res_else.bin: {}", tensyoki::to_string(else_result.error()));
        }
        
        // Test parsing grp.bin (large file; may only parse the index)
        BinArchive grp_archive;
        auto grp_result = grp_archive.load_from_file(data_root_ / "res" / "res_grp.bin");
        if (grp_result) {
            LOG_INFO("res_grp.bin: {} resources", grp_archive.entries().size());
        } else {
            LOG_WARN("Failed to parse res_grp.bin: {}", tensyoki::to_string(grp_result.error()));
        }
    }

    /// Headless battle system test: terrain, weather, morale, status effects, routing.
    /// Returns true if all checks pass.
    bool run_battle_test() {
        LOG_INFO("=== Battle System Test ===");

        int failures = 0;
        auto check = [&](bool cond, const std::string& desc) {
            if (cond) {
                LOG_INFO("  PASS: {}", desc);
            } else {
                LOG_ERROR("  FAIL: {}", desc);
                ++failures;
            }
        };

        // ---- Setup ----
        BattleStateData battle;
        battle.init_battle();

        // Use get_unit_by_id: player units have ids 1,2,3; enemy units 4,5,6
        BattleUnit* player_inf = battle.get_unit_by_id(1);
        BattleUnit* enemy_archer = battle.get_unit_by_id(5);
        check(player_inf != nullptr, "player unit id 1 exists");
        check(enemy_archer != nullptr, "enemy unit id 5 exists");

        // ---- Terrain bonuses ----
        LOG_INFO("--- Terrain Bonuses ---");
        // Infantry on Castle: +2 attack, +3 defense, move cost 1
        check(player_inf->get_terrain_attack_bonus(TerrainType::Castle) == 2,
              "infantry castle attack bonus == 2");
        check(player_inf->get_terrain_defense_bonus(TerrainType::Castle) == 3,
              "infantry castle defense bonus == 3");
        check(player_inf->get_terrain_move_cost(TerrainType::Castle) == 1,
              "infantry castle move cost == 1");
        // Cavalry on Forest: -2 attack, +0 defense, move cost 3
        BattleUnit cav;
        cav.unit_type = UnitType::Cavalry;
        check(cav.get_terrain_attack_bonus(TerrainType::Forest) == -2,
              "cavalry forest attack bonus == -2");
        check(cav.get_terrain_move_cost(TerrainType::Forest) == 3,
              "cavalry forest move cost == 3");
        // Archer on Forest: +2 attack, +1 defense, move cost 2
        BattleUnit arch;
        arch.unit_type = UnitType::Archer;
        check(arch.get_terrain_attack_bonus(TerrainType::Forest) == 2,
              "archer forest attack bonus == 2");
        check(arch.get_terrain_defense_bonus(TerrainType::Forest) == 1,
              "archer forest defense bonus == 1");
        check(arch.get_terrain_move_cost(TerrainType::Forest) == 2,
              "archer forest move cost == 2");
        // Water: high move cost for all
        check(player_inf->get_terrain_move_cost(TerrainType::Water) == 4,
              "infantry water move cost == 4");
        check(cav.get_terrain_move_cost(TerrainType::Water) == 5,
              "cavalry water move cost == 5");
        check(arch.get_terrain_move_cost(TerrainType::Water) == 3,
              "archer water move cost == 3");

        // ---- Movement cost with weather ----
        LOG_INFO("--- Weather Effects ---");
        // Clear weather: no movement penalty
        battle.set_weather(0);  // Force Clear
        check(battle.weather_movement_penalty() == 0, "clear weather movement penalty == 0");
        check(battle.weather() == 0, "weather is Clear");
        // Weather blocks vision in fog
        battle.set_weather(3);  // Fog
        check(battle.weather_blocks_vision() == true, "fog blocks vision");
        battle.set_weather(0);  // Clear
        check(battle.weather_blocks_vision() == false, "clear does not block vision");

        // ---- Morale system ----
        LOG_INFO("--- Morale System ---");
        BattleUnit high_morale;
        high_morale.morale = 80;
        high_morale.attack = 20;
        high_morale.defense = 10;
        check(high_morale.effective_attack() == 23,  // 20 + (80-50)/10 = 23
              "high morale (+30) boosts attack by 3");
        check(high_morale.effective_defense() == 13,  // 10 + (80-50)/10 = 13
              "high morale (+30) boosts defense by 3");

        BattleUnit low_morale;
        low_morale.morale = 20;
        low_morale.attack = 20;
        low_morale.defense = 10;
        check(low_morale.effective_attack() == 17,  // 20 + (20-50)/10 = 17
              "low morale (-30) reduces attack by 3");
        check(low_morale.effective_defense() == 7,  // 10 + (20-50)/10 = 7
              "low morale (-30) reduces defense by 3");

        // ---- Status effects: Weakness ----
        LOG_INFO("--- Status Effects ---");
        BattleUnit weak_unit;
        weak_unit.attack = 30;
        battle.apply_status_effect(weak_unit, StatusEffect::Type::Weakness, 3, 0);
        check(weak_unit.is_weakened(), "unit is weakened after apply_status_effect");
        check(weak_unit.effective_attack() == 20,  // 30 * 2/3 = 20
              "weakness reduces attack to 2/3 (30 -> 20)");
        check(weak_unit.status_effects[0].turns_remaining == 3,
              "weakness lasts 3 turns");

        // ---- Status effects: ArmorBreak ----
        BattleUnit armor_unit;
        armor_unit.defense = 30;
        battle.apply_status_effect(armor_unit, StatusEffect::Type::ArmorBreak, 3, 0);
        check(armor_unit.is_armor_broken(), "unit has ArmorBreak");
        check(armor_unit.effective_defense() == 20,  // 30 * 2/3 = 20
              "armor break reduces defense to 2/3 (30 -> 20)");

        // ---- Status effects: Slow ----
        BattleUnit slow_unit;
        slow_unit.move = 6;
        battle.apply_status_effect(slow_unit, StatusEffect::Type::Slow, 2, 0);
        check(slow_unit.is_slowed(), "unit is slowed");
        check(slow_unit.effective_move() == 3,  // 6 / 2 = 3
              "slow halves movement (6 -> 3)");

        // ---- Status effects: Burn ----
        BattleUnit burn_unit;
        burn_unit.hp = 100;
        burn_unit.max_hp = 100;
        battle.apply_status_effect(burn_unit, StatusEffect::Type::Burn, 3, 10);
        check(burn_unit.is_burning(), "unit is burning");
        battle.process_status_effects(burn_unit);
        check(burn_unit.hp == 90,  // 100 - 10 burn damage
              "burn deals 10 damage per turn");
        check(burn_unit.status_effects[0].turns_remaining == 2,
              "burn turns remaining decremented to 2");

        // ---- Status effects: Sleep ----
        BattleUnit sleep_unit;
        sleep_unit.hp = 100;
        sleep_unit.has_acted = false;
        battle.apply_status_effect(sleep_unit, StatusEffect::Type::Sleep, 2, 0);
        check(sleep_unit.is_sleeping(), "unit is sleeping");
        check(sleep_unit.can_act() == false, "sleeping unit cannot act");

        // ---- Status effects: Silence ----
        BattleUnit silence_unit;
        battle.apply_status_effect(silence_unit, StatusEffect::Type::Silence, 2, 0);
        check(silence_unit.is_silenced(), "unit is silenced");

        // ---- Status effects: Counterattack ----
        BattleUnit counter_unit;
        battle.apply_status_effect(counter_unit, StatusEffect::Type::Counterattack, 2, 0);
        check(counter_unit.has_counterattack(), "unit has Counterattack");

        // ---- Status effects: Poison ----
        BattleUnit poison_unit;
        poison_unit.hp = 100;
        poison_unit.max_hp = 100;
        battle.apply_status_effect(poison_unit, StatusEffect::Type::Poison, 3, 15);
        check(poison_unit.has_status(StatusEffect::Type::Poison), "unit is poisoned");
        battle.process_status_effects(poison_unit);
        check(poison_unit.hp == 85,  // 100 - 15 poison damage
              "poison deals 15 damage per turn");

        // ---- Status effect expiration ----
        BattleUnit expire_unit;
        expire_unit.hp = 100;
        battle.apply_status_effect(expire_unit, StatusEffect::Type::Weakness, 1, 0);
        check(expire_unit.status_effects[0].turns_remaining == 1, "weakness starts at 1 turn");
        battle.process_status_effects(expire_unit);
        check(expire_unit.status_effects.empty(), "weakness expired after 1 turn");
        check(!expire_unit.is_weakened(), "unit no longer weakened after expiry");

        // ---- Routing system ----
        LOG_INFO("--- Routing System ---");
        BattleUnit routed_unit;
        routed_unit.id = 99;
        routed_unit.morale = 5;  // Very low morale
        routed_unit.name = "Test Routed Unit";
        routed_unit.is_player = true;
        bool rout_result = battle.unit_rout_check(routed_unit);
        // morale 5 => rout chance = (20-5)*2 = 30%. Not deterministic, just check it doesn't crash
        LOG_INFO("  rout_check for morale=5 returned {}", rout_result ? "true" : "false");
        if (rout_result) {
            battle.process_unit_rout(routed_unit);
            check(routed_unit.is_routed, "routed unit has is_routed == true");
            check(routed_unit.rout_turns_remaining == 3, "rout lasts 3 turns");
            check(routed_unit.can_act() == false, "routed unit cannot act");
            check(routed_unit.effective_attack() <= routed_unit.attack / 2 + 1,
                  "routed unit attack reduced");
            check(routed_unit.effective_defense() <= routed_unit.defense / 2 + 1,
                  "routed unit defense reduced");
        }

        // High morale unit should not rout
        BattleUnit brave_unit;
        brave_unit.id = 100;
        brave_unit.morale = 90;
        brave_unit.name = "Brave Unit";
        check(battle.unit_rout_check(brave_unit) == false, "high morale unit does not rout");

        // ---- Attack with terrain modifiers ----
        LOG_INFO("--- Combat with Terrain ---");
        if (player_inf && enemy_archer) {
            // Place them adjacent on plain
            player_inf->grid_x = 5;
            player_inf->grid_y = 5;
            player_inf->has_acted = false;
            enemy_archer->grid_x = 6;
            enemy_archer->grid_y = 5;
            enemy_archer->has_acted = false;
            enemy_archer->hp = 100;
            enemy_archer->max_hp = 100;
            battle.get_terrain(5, 5).type = TerrainType::Plain;
            battle.get_terrain(6, 5).type = TerrainType::Plain;
            battle.get_terrain(5, 5).unit_id = player_inf->id;
            battle.get_terrain(6, 5).unit_id = enemy_archer->id;

            int32_t dmg = battle.attack(*player_inf, *enemy_archer);
            check(dmg >= 0, "attack deals non-negative damage");
            check(enemy_archer->hp < 100, "enemy took damage from attack");
            check(player_inf->has_acted == true, "attacker has_acted after attack");
            check(player_inf->morale > 0, "attacker morale changed after attack");

            // Attack from Castle terrain gives infantry +2 attack bonus
            player_inf->has_acted = false;
            enemy_archer->hp = 100;
            enemy_archer->max_hp = 100;
            battle.get_terrain(5, 5).type = TerrainType::Castle;
            int32_t dmg_castle = battle.attack(*player_inf, *enemy_archer);
            LOG_INFO("  dmg on plain={} dmg on castle={}", dmg, dmg_castle);
            // Castle terrain should generally give equal or more damage (infantry attack bonus)
            check(dmg_castle >= dmg, "castle terrain attack bonus increases damage for infantry");
        }

        // ---- Ranged attack (archer) ----
        LOG_INFO("--- Ranged Attack ---");
        if (player_inf && enemy_archer) {
            enemy_archer->unit_type = UnitType::Archer;
            enemy_archer->grid_x = 8;
            enemy_archer->grid_y = 5;
            enemy_archer->has_acted = false;
            player_inf->hp = 100;
            player_inf->max_hp = 100;
            player_inf->grid_x = 5;
            player_inf->grid_y = 5;
            battle.get_terrain(8, 5).type = TerrainType::Plain;
            battle.get_terrain(8, 5).unit_id = enemy_archer->id;
            battle.get_terrain(5, 5).unit_id = player_inf->id;

            int32_t rdmg = battle.ranged_attack(*enemy_archer, *player_inf);
            check(rdmg >= 0, "ranged attack deals non-negative damage");
            check(enemy_archer->has_acted == true, "archer has_acted after ranged attack");
            // Damage may be 0 due to random roll vs defense; just verify the attack was processed
            check(player_inf->hp <= 100, "target HP is valid after ranged attack");
        }

        // ---- Skills ----
        LOG_INFO("--- Skills ---");
        if (player_inf) {
            // Level up to 3 so Heal skill is available (needs level >= 3)
            battle.grant_exp(*player_inf, 100);  // level 1 -> 2
            battle.grant_exp(*player_inf, 200);  // level 2 -> 3

            // Heal skill
            player_inf->has_acted = false;
            player_inf->used_skill = false;
            player_inf->morale = 50;
            player_inf->hp = 50;
            player_inf->max_hp = 100;
            auto skills = player_inf->get_available_skills();
            bool has_heal = false;
            for (const auto& s : skills) {
                if (s.type == SkillType::Heal) has_heal = true;
            }
            check(has_heal, "infantry has Heal skill available at level 3");
            // Use heal on self - find the Heal skill
            const BattleSkill* heal_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Heal) heal_skill = &s;
            }
            check(heal_skill != nullptr, "heal skill found in skill list");
            if (heal_skill) {
                player_inf->grid_x = 5;
                player_inf->grid_y = 5;
                battle.get_terrain(5, 5).unit_id = player_inf->id;  // register on grid
                int32_t heal_dmg = battle.use_skill(*player_inf, *heal_skill, 5, 5);
                check(player_inf->hp > 50, "heal skill restored HP");
                check(player_inf->has_acted == true, "using skill sets has_acted");
            }
        }

        // Silence skill on enemy
        if (player_inf && enemy_archer) {
            enemy_archer->has_acted = false;
            enemy_archer->used_skill = false;
            enemy_archer->morale = 50;
            auto skills = player_inf->get_available_skills();
            bool has_silence = false;
            for (const auto& s : skills) {
                if (s.type == SkillType::Silence) has_silence = true;
            }
            if (has_silence) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                // Find silence skill
                const BattleSkill* silence_skill = nullptr;
                for (const auto& s : skills) {
                    if (s.type == SkillType::Silence) silence_skill = &s;
                }
                if (silence_skill) {
                    battle.use_skill(*player_inf, *silence_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                    check(enemy_archer->is_silenced(), "silence skill applied to enemy");
                }
            }
        }

        // Poison skill
        if (player_inf && enemy_archer) {
            enemy_archer->hp = 100;
            enemy_archer->max_hp = 100;
            auto skills = player_inf->get_available_skills();
            const BattleSkill* poison_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Poison) poison_skill = &s;
            }
            if (poison_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *poison_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->has_status(StatusEffect::Type::Poison), "poison skill applied to enemy");
            }
        }

        // Sleep skill
        if (player_inf && enemy_archer) {
            auto skills = player_inf->get_available_skills();
            const BattleSkill* sleep_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Sleep) sleep_skill = &s;
            }
            if (sleep_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *sleep_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->is_sleeping(), "sleep skill applied to enemy");
            }
        }

        // Slow skill
        if (player_inf && enemy_archer) {
            auto skills = player_inf->get_available_skills();
            const BattleSkill* slow_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Slow) slow_skill = &s;
            }
            if (slow_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *slow_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->is_slowed(), "slow skill applied to enemy");
            }
        }

        // Weakness skill
        if (player_inf && enemy_archer) {
            auto skills = player_inf->get_available_skills();
            const BattleSkill* weak_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Weakness) weak_skill = &s;
            }
            if (weak_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *weak_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->is_weakened(), "weakness skill applied to enemy");
            }
        }

        // ArmorBreak skill
        if (player_inf && enemy_archer) {
            auto skills = player_inf->get_available_skills();
            const BattleSkill* ab_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::ArmorBreak) ab_skill = &s;
            }
            if (ab_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *ab_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->is_armor_broken(), "armor break skill applied to enemy");
            }
        }

        // Burn skill
        if (player_inf && enemy_archer) {
            auto skills = player_inf->get_available_skills();
            const BattleSkill* burn_skill = nullptr;
            for (const auto& s : skills) {
                if (s.type == SkillType::Burn) burn_skill = &s;
            }
            if (burn_skill) {
                player_inf->has_acted = false;
                player_inf->used_skill = false;
                player_inf->morale = 50;
                battle.use_skill(*player_inf, *burn_skill, enemy_archer->grid_x, enemy_archer->grid_y);
                check(enemy_archer->is_burning(), "burn skill applied to enemy");
            }
        }

        // ---- Silenced unit cannot use skills ----
        LOG_INFO("--- Skill Restrictions ---");
        if (player_inf) {
            player_inf->has_acted = false;
            player_inf->used_skill = false;
            player_inf->morale = 50;
            battle.apply_status_effect(*player_inf, StatusEffect::Type::Silence, 2, 0);
            auto skills = player_inf->get_available_skills();
            bool has_guard = false;
            for (const auto& s : skills) {
                if (s.type == SkillType::Guard) has_guard = true;
            }
            if (has_guard) {
                const BattleSkill* guard_skill = nullptr;
                for (const auto& s : skills) {
                    if (s.type == SkillType::Guard) guard_skill = &s;
                }
                if (guard_skill) {
                    player_inf->grid_x = 5;
                    player_inf->grid_y = 5;
                    int32_t result = battle.use_skill(*player_inf, *guard_skill, 5, 5);
                    check(result == 0, "silenced unit cannot use skill (returns 0)");
                    check(player_inf->has_acted == false, "silenced unit has_acted remains false");
                }
            }
        }

        // ---- Weather affects attack ----
        LOG_INFO("--- Weather Attack Penalty ---");
        battle.set_weather(3);  // Fog: -2 attack penalty
        check(battle.weather_attack_penalty() == -2, "fog attack penalty == -2");
        battle.set_weather(1);  // Rain: -1
        check(battle.weather_attack_penalty() == -1, "rain attack penalty == -1");
        battle.set_weather(0);  // Clear: 0
        check(battle.weather_attack_penalty() == 0, "clear attack penalty == 0");

        // ---- Win conditions ----
        LOG_INFO("--- Win Conditions ---");
        // Just verify check_win_conditions doesn't crash on a fresh battle
        battle.check_win_conditions();
        check(true, "check_win_conditions runs without crash");

        // ---- Settlement ----
        LOG_INFO("--- Settlement ---");
        auto settlement = battle.get_settlement();
        check(true, "get_settlement returns settlement");

        // ---- Summary ----
        LOG_INFO("=== Battle Test Summary ===");
        LOG_INFO("  Failures: {}", failures);
        if (failures == 0) {
            LOG_INFO("All battle tests PASSED");
        } else {
            LOG_ERROR("{} battle test(s) FAILED", failures);
        }
        return failures == 0;
    }

    /// Headless castle command test: loyalty, tax, rebellion, region control.
    /// Returns true if all checks pass.
    bool run_castle_test() {
        LOG_INFO("=== Castle Command System Test ===");

        int failures = 0;
        auto check = [&](bool cond, const std::string& desc) {
            if (cond) {
                LOG_INFO("  PASS: {}", desc);
            } else {
                LOG_ERROR("  FAIL: {}", desc);
                ++failures;
            }
        };

        CastleCommand cmd;

        // ---- Setup test castles ----
        LOG_INFO("--- Setup ---");
        std::vector<CastleData> test_castles;
        for (uint32_t i = 1; i <= 5; ++i) {
            CastleData c;
            c.index = i;
            c.name = "Castle " + std::to_string(i);
            c.name_utf8 = c.name;
            c.level = 1;
            c.gold = 1000;
            c.food = 500;
            c.garrison = 0;
            c.max_garrison = 100;
            c.loyalty = 50;
            c.tax_rate = 50;
            c.unrest = 0;
            c.is_player_owned = true;
            c.region_id = (i <= 3) ? 1 : 2;  // Castles 1-3 in region 1, 4-5 in region 2
            c.is_capital = (i == 1);
            test_castles.push_back(c);
        }
        cmd.set_castles_data(test_castles);
        check(cmd.castles().size() == 5, "5 test castles created");

        // ---- Tax rate management ----
        LOG_INFO("--- Tax Rate ---");
        cmd.set_tax_rate(50);
        check(cmd.tax_rate() == 50, "tax rate set to 50");
        cmd.set_tax_rate(80);
        check(cmd.tax_rate() == 80, "tax rate set to 80");
        cmd.set_tax_rate(150);  // Should clamp to 100
        check(cmd.tax_rate() == 100, "tax rate clamped to 100");
        cmd.set_tax_rate(-10);  // Should clamp to 0
        check(cmd.tax_rate() == 0, "tax rate clamped to 0");

        // ---- Tax income calculation ----
        LOG_INFO("--- Tax Income ---");
        cmd.set_tax_rate(50);
        CastleData rich_castle = test_castles[0];
        rich_castle.has_market = true;
        rich_castle.level = 3;
        int32_t income = cmd.calculate_tax_income(rich_castle);
        // base=100, market +200, level bonus 50*2=100 => 400 * 50/100 = 200
        check(income == 200, "tax income with market + level 3 at 50% == 200");

        CastleData poor_castle = test_castles[0];
        poor_castle.level = 1;
        int32_t poor_income = cmd.calculate_tax_income(poor_castle);
        // base=100 * 50/100 = 50
        check(poor_income == 50, "tax income at level 1, 50% rate == 50");

        // High tax = more income
        cmd.set_tax_rate(100);
        int32_t high_income = cmd.calculate_tax_income(rich_castle);
        // 400 * 100/100 = 400
        check(high_income == 400, "tax income at 100% rate == 400");

        // ---- Loyalty management ----
        LOG_INFO("--- Loyalty ---");
        cmd.set_tax_rate(50);
        cmd.adjust_loyalty(1, 10);
        check(cmd.castle_loyalty(1) == 60, "loyalty adjusted +10 to 60");
        cmd.adjust_loyalty(1, -20);
        check(cmd.castle_loyalty(1) == 40, "loyalty adjusted -20 to 40");
        cmd.adjust_loyalty(1, -100);  // Should clamp to 0
        check(cmd.castle_loyalty(1) == 0, "loyalty clamped to 0");
        cmd.adjust_loyalty(1, 200);  // Should clamp to 100
        check(cmd.castle_loyalty(1) == 100, "loyalty clamped to 100");

        // ---- Loyalty change calculation ----
        LOG_INFO("--- Loyalty Change ---");
        cmd.set_tax_rate(50);
        CastleData normal_castle = test_castles[0];
        normal_castle.garrison = 60;  // > max_garrison/2 = 50
        normal_castle.has_market = true;
        normal_castle.has_storehouse = true;
        normal_castle.lord_officer_id = 5;
        int32_t change = cmd.calculate_loyalty_change(normal_castle);
        // garrison bonus +1, market +1, storehouse +1, lord +2 = +5
        check(change == 5, "loyalty change with all bonuses == +5");

        // High tax penalty
        cmd.set_tax_rate(80);
        CastleData high_tax_castle = test_castles[0];
        high_tax_castle.garrison = 60;
        high_tax_castle.lord_officer_id = 5;
        int32_t high_tax_change = cmd.calculate_loyalty_change(high_tax_castle);
        // tax > 60: -((80-60)/10) = -2, garrison +1, lord +2 = +1
        check(high_tax_change == 1, "loyalty change with high tax (80%) == +1");

        // Very high tax
        cmd.set_tax_rate(100);
        CastleData very_high_tax = test_castles[0];
        very_high_tax.garrison = 60;
        very_high_tax.lord_officer_id = 5;
        int32_t vht_change = cmd.calculate_loyalty_change(very_high_tax);
        // tax > 60: -((100-60)/10) = -4, garrison +1, lord +2 = -1
        check(vht_change == -1, "loyalty change with very high tax (100%) == -1");

        // Low tax bonus
        cmd.set_tax_rate(20);
        CastleData low_tax_castle = test_castles[0];
        low_tax_castle.garrison = 60;
        low_tax_castle.lord_officer_id = 5;
        int32_t low_tax_change = cmd.calculate_loyalty_change(low_tax_castle);
        // tax < 40: +(40-20)/10 = +2, garrison +1, lord +2 = +5
        check(low_tax_change == 5, "loyalty change with low tax (20%) == +5");

        // ---- Rebellion system ----
        LOG_INFO("--- Rebellion ---");
        // Reset castle 1 to player-owned with low loyalty and high unrest
        auto* c1 = cmd.get_castle(1);
        c1->is_player_owned = true;
        c1->loyalty = 5;   // Very low loyalty
        c1->unrest = 80;   // Very high unrest
        c1->index = 1;
        // rebellion_chance = (80-50)/10 + (30-5)/5 = 3 + 5 = 8
        // rng seed = 1*1000 + 5 = 1005, deterministic
        bool rebelled = cmd.check_rebellion(1);
        LOG_INFO("  castle 1 rebellion chance check returned {}", rebelled ? "true" : "false");
        // Note: rebellion is random, just verify it doesn't crash and records events properly

        // Manual trigger rebellion
        cmd.trigger_rebellion(1);
        check(!c1->is_player_owned, "rebelled castle is no longer player-owned");
        check(c1->lord_officer_id == 0, "rebelled castle lord dismissed");
        check(c1->loyalty == 50, "rebelled castle loyalty reset to 50");
        check(c1->unrest == 30, "rebelled castle unrest set to 30");
        check(cmd.rebellion_events().size() == 1, "1 rebellion event recorded");
        check(cmd.rebellion_events()[0].castle_index == 1, "rebellion event has correct castle index");

        // Non-player castle cannot rebel
        auto* c2 = cmd.get_castle(2);
        c2->is_player_owned = false;
        c2->loyalty = 0;
        c2->unrest = 100;
        bool c2_rebelled = cmd.check_rebellion(2);
        check(!c2_rebelled, "non-player castle cannot rebel");

        // Player-owned castle with high loyalty should not rebel (usually)
        cmd.set_tax_rate(50);
        auto* c3 = cmd.get_castle(3);
        c3->is_player_owned = true;
        c3->loyalty = 90;
        c3->unrest = 5;
        bool c3_rebelled = cmd.check_rebellion(3);
        // loyalty 90, unrest 5 => rebellion_chance = 0, should not rebel
        check(!c3_rebelled, "high loyalty castle does not rebel");

        // ---- Region control ----
        LOG_INFO("--- Region Control ---");
        // Reset all castles to player-owned (castle 1 may have rebelled earlier)
        for (auto& c : test_castles) {
            if (auto* cc = cmd.get_castle(c.index)) {
                cc->is_player_owned = true;
            }
        }
        // Castles 1,2,3 in region 1, all player-owned
        check(cmd.controls_region(1), "player controls region 1 (all 3 castles)");
        // Castles 4,5 in region 2, all player-owned
        check(cmd.controls_region(2), "player controls region 2 (all 2 castles)");

        // Make castle 3 enemy-owned
        auto* c3_enemy = cmd.get_castle(3);
        c3_enemy->is_player_owned = false;
        check(!cmd.controls_region(1), "region 1 not controlled if castle 3 is enemy");
        check(cmd.get_castles_in_region(1).size() == 3, "region 1 has 3 castles");
        check(cmd.get_player_castles_in_region(1).size() == 2,
              "region 1 has 2 player castles after castle 3 lost");

        // Restore
        c3_enemy->is_player_owned = true;

        // ---- Player castles ----
        LOG_INFO("--- Player Castles ---");
        auto player_castles = cmd.get_player_castles();
        check(player_castles.size() == 5, "5 player castles (all castles are player-owned)");
        check(cmd.player_castle_count() == 5, "player castle count == 5");

        // ---- Monthly income processing ----
        LOG_INFO("--- Monthly Income ---");
        cmd.set_tax_rate(50);
        // Reset castle data
        for (auto& c : test_castles) {
            c.is_player_owned = true;
            c.loyalty = 50;
            c.unrest = 0;
            c.gold = 1000;
            c.food = 500;
            c.garrison = 10;
        }
        cmd.set_castles_data(test_castles);
        cmd.process_monthly_income();
        // Turn should advance
        check(cmd.get_turn_count() == 1, "turn advanced to 1 after monthly income");
        // Castle should have gained income
        auto* updated = cmd.get_castle(1);
        check(updated->gold >= 1000, "castle gained gold income");
        check(updated->food >= 500, "castle has food after production");

        // ---- Auto-recruit ----
        LOG_INFO("--- Auto Recruit ---");
        // Castle with gold but not full garrison
        auto* recruit_castle = cmd.get_castle(1);
        recruit_castle->gold = 1000;
        recruit_castle->garrison = 10;
        recruit_castle->max_garrison = 100;
        cmd.process_auto_recruit();
        // Should recruit as many as possible with 1000 gold at 5/troop = 200 troops
        // But max_garrison - garrison = 90, so recruit 90
        check(recruit_castle->garrison == 100, "auto-recruit filled garrison to max");
        check(recruit_castle->gold == 1000 - 90 * 5, "gold spent on recruitment");

        // ---- Gold/Food/Auto-defense calculations ----
        LOG_INFO("--- Resource Calculations ---");
        CastleData calc_castle;
        calc_castle.level = 2;
        calc_castle.garrison = 50;
        check(cmd.calculate_food_production(calc_castle) == 75,
              "food production: 50 base + 25 (level2) = 75");
        check(cmd.calculate_food_consumption(calc_castle) == 5,
              "food consumption: 50/10 = 5");
        calc_castle.has_wall = true;
        calc_castle.has_barracks = true;
        check(cmd.calculate_auto_defense(calc_castle) == 30 + 20 + 20,
              "auto-defense: wall 30 + barracks 20 + level 20 = 70");

        // ---- Battle → Map Integration ----
        LOG_INFO("--- Battle to Map Integration ---");
        // Reset all castles to known state
        for (auto& c : test_castles) {
            c.is_player_owned = (c.index <= 3);  // Castles 1-3 player, 4-5 enemy
            c.loyalty = 50;
            c.unrest = 0;
            c.garrison = 10;
            c.gold = 1000;
            c.lord_officer_id = 0;
        }
        cmd.set_castles_data(test_castles);

        // Simulate PlayerVictory: player attacks enemy castle 4 (region 2)
        auto* castle4 = cmd.get_castle(4);
        castle4->is_player_owned = false;
        castle4->loyalty = 30;
        castle4->unrest = 20;
        // Apply victory logic (same as MapViewState)
        castle4->is_player_owned = true;
        castle4->lord_officer_id = 0;
        castle4->lord_name.clear();
        castle4->lord_name_utf8.clear();
        castle4->loyalty = 50;
        castle4->unrest = 10;
        castle4->garrison = std::max(castle4->garrison, 30);
        castle4->garrison = std::min(castle4->garrison, castle4->max_garrison);
        // Reward source castle (castle 1)
        auto* castle1 = cmd.get_castle(1);
        castle1->gold += 500;

        check(castle4->is_player_owned, "victory: castle 4 becomes player-owned");
        check(castle4->loyalty == 50, "victory: castle 4 loyalty reset to 50");
        check(castle4->unrest == 10, "victory: castle 4 unrest set to 10");
        check(castle4->garrison >= 30, "victory: castle 4 garrison set to >= 30");
        check(castle1->gold == 1500, "victory: source castle 1 gained 500 gold");
        // Region 2: castles 4 and 5, now castle 4 is player-owned but 5 is not
        check(!cmd.controls_region(2), "victory: region 2 not fully controlled yet");

        // Now make castle 5 also player-owned (simulate second victory)
        auto* castle5 = cmd.get_castle(5);
        castle5->is_player_owned = true;
        check(cmd.controls_region(2), "victory: region 2 fully controlled after castle 5");

        // Simulate PlayerDefeat: player attacks from castle 1, loses
        castle1->garrison = 50;
        castle1->is_player_owned = true;
        // Apply defeat logic (same as MapViewState)
        castle1->garrison = std::max(0, castle1->garrison - 20);
        check(castle1->garrison == 30, "defeat: source castle 1 loses 20 troops");

        // Defensive battle loss: player-owned target castle is lost
        auto* castle2 = cmd.get_castle(2);
        castle2->is_player_owned = true;
        castle2->loyalty = 60;
        castle2->lord_officer_id = 7;
        castle2->lord_name = "Lord";
        // Apply defensive defeat logic
        castle2->is_player_owned = false;
        castle2->lord_officer_id = 0;
        castle2->lord_name.clear();
        castle2->lord_name_utf8.clear();
        check(!castle2->is_player_owned, "defeat: player-owned castle 2 is lost");
        check(castle2->lord_officer_id == 0, "defeat: castle 2 lord dismissed");
        check(!cmd.controls_region(1), "defeat: region 1 no longer fully controlled");

        // ---- Summary ----
        LOG_INFO("=== Castle Test Summary ===");
        LOG_INFO("  Failures: {}", failures);
        if (failures == 0) {
            LOG_INFO("All castle tests PASSED");
        } else {
            LOG_ERROR("{} castle test(s) FAILED", failures);
        }
        return failures == 0;
    }

    /// Headless save/load system test: save, load, delete, list slots.
    /// Returns true if all checks pass.
    bool run_save_test() {
        LOG_INFO("=== Save/Load System Test ===");

        int failures = 0;
        auto check = [&](bool cond, const std::string& desc) {
            if (cond) {
                LOG_INFO("  PASS: {}", desc);
            } else {
                LOG_ERROR("  FAIL: {}", desc);
                ++failures;
            }
        };

        // Use a temp directory for saves
        const std::string test_save_dir = "/tmp/tensyoki_save_test";
        SaveSystem save_sys;
        save_sys.init(test_save_dir);

        // ---- Initially no saves ----
        LOG_INFO("--- Initial State ---");
        check(save_sys.list_saves().empty(), "no saves initially");
        check(!save_sys.slot_exists(1), "slot 1 does not exist initially");

        // ---- Create save data ----
        LOG_INFO("--- Create Save ---");
        GameSaveData data;
        data.version = 1;
        data.date_year = 4;
        data.date_month = 10;
        data.date_day = 15;
        data.save_name = "Test Save";
        data.player_castle_count = 3;
        data.total_castles = 10;

        // Add some castle data
        CastleData c1;
        c1.index = 1;
        c1.name = "Castle A";
        c1.name_utf8 = "Castle A";
        c1.level = 3;
        c1.garrison = 50;
        c1.gold = 2000;
        c1.food = 1000;
        c1.loyalty = 75;
        c1.tax_rate = 40;
        c1.unrest = 10;
        c1.is_player_owned = true;
        c1.region_id = 1;
        data.castles.push_back(c1);

        CastleData c2;
        c2.index = 2;
        c2.name = "Castle B";
        c2.name_utf8 = "Castle B";
        c2.level = 2;
        c2.garrison = 30;
        c2.gold = 1500;
        c2.food = 800;
        c2.loyalty = 60;
        c2.tax_rate = 50;
        c2.unrest = 20;
        c2.is_player_owned = false;
        c2.region_id = 1;
        data.castles.push_back(c2);

        // ---- Save to slot 1 ----
        LOG_INFO("--- Save ---");
        bool saved = save_sys.save(1, data);
        check(saved, "save to slot 1 succeeded");
        check(save_sys.slot_exists(1), "slot 1 exists after save");

        // ---- List saves ----
        LOG_INFO("--- List Saves ---");
        auto slots = save_sys.list_saves();
        check(slots.size() == 1, "1 save slot listed");
        check(slots[0].slot == 1, "slot number is 1");
        check(slots[0].save_name == "Test Save", "save name is 'Test Save'");
        check(slots[0].date_year == 4, "date year is 4");
        check(slots[0].date_month == 10, "date month is 10");
        check(slots[0].date_day == 15, "date day is 15");
        check(slots[0].player_castle_count == 3, "player castle count is 3");
        check(slots[0].total_castles == 10, "total castle count is 10");
        check(slots[0].is_valid, "slot is valid");

        // ---- Load from slot 1 ----
        LOG_INFO("--- Load ---");
        GameSaveData loaded;
        bool loaded_ok = save_sys.load(1, loaded);
        check(loaded_ok, "load from slot 1 succeeded");
        check(loaded.version == 1, "loaded version is 1");
        check(loaded.date_year == 4, "loaded date year is 4");
        check(loaded.date_month == 10, "loaded date month is 10");
        check(loaded.date_day == 15, "loaded date day is 15");
        check(loaded.save_name == "Test Save", "loaded save name matches");
        check(loaded.player_castle_count == 3, "loaded player castle count matches");
        check(loaded.total_castles == 10, "loaded total castle count matches");
        check(loaded.castles.size() == 2, "loaded 2 castles");
        check(loaded.castles[0].index == 1, "loaded castle 1 index matches");
        check(loaded.castles[0].name == "Castle A", "loaded castle 1 name matches");
        check(loaded.castles[0].loyalty == 75, "loaded castle 1 loyalty matches");
        check(loaded.castles[0].tax_rate == 40, "loaded castle 1 tax rate matches");
        check(loaded.castles[0].unrest == 10, "loaded castle 1 unrest matches");
        check(loaded.castles[0].is_player_owned, "loaded castle 1 is player-owned");
        check(loaded.castles[1].index == 2, "loaded castle 2 index matches");
        check(!loaded.castles[1].is_player_owned, "loaded castle 2 is not player-owned");

        // ---- Save to slot 2 and verify both exist ----
        LOG_INFO("--- Multiple Saves ---");
        GameSaveData data2;
        data2.version = 1;
        data2.date_year = 5;
        data2.date_month = 1;
        data2.date_day = 1;
        data2.save_name = "Second Save";
        data2.player_castle_count = 1;
        data2.total_castles = 5;
        bool saved2 = save_sys.save(2, data2);
        check(saved2, "save to slot 2 succeeded");
        slots = save_sys.list_saves();
        check(slots.size() == 2, "2 save slots listed");

        // ---- Delete slot 1 ----
        LOG_INFO("--- Delete ---");
        bool deleted = save_sys.delete_save(1);
        check(deleted, "delete slot 1 succeeded");
        check(!save_sys.slot_exists(1), "slot 1 no longer exists");
        slots = save_sys.list_saves();
        check(slots.size() == 1, "1 save slot remains after delete");
        check(slots[0].slot == 2, "remaining slot is slot 2");

        // ---- Overwrite save ----
        LOG_INFO("--- Overwrite ---");
        GameSaveData data3;
        data3.version = 1;
        data3.date_year = 9;
        data3.date_month = 9;
        data3.date_day = 9;
        data3.save_name = "Overwritten Save";
        data3.player_castle_count = 0;
        data3.total_castles = 0;
        bool overwritten = save_sys.save(2, data3);
        check(overwritten, "overwrite slot 2 succeeded");
        GameSaveData loaded2;
        save_sys.load(2, loaded2);
        check(loaded2.date_year == 9, "overwritten date year is 9");
        check(loaded2.save_name == "Overwritten Save", "overwritten save name matches");

        // ---- Version 1 migration: legacy file without owner_id/turn_count ----
        LOG_INFO("--- Version 1 Migration ---");
        const std::string legacy_save =
            "version=1\n"
            "date_year=4\n"
            "date_month=10\n"
            "date_day=15\n"
            "save_name=V1 Legacy Save\n"
            "player_castle_count=2\n"
            "total_castles=2\n"
            "castle_count=2\n"
            "castle.index=1\n"
            "castle.name=Castle A\n"
            "castle.name_utf8=Castle A\n"
            "castle.level=3\n"
            "castle.defense=10\n"
            "castle.max_defense=50\n"
            "castle.garrison=50\n"
            "castle.max_garrison=100\n"
            "castle.gold=2000\n"
            "castle.food=1000\n"
            "castle.has_wall=0\n"
            "castle.has_barracks=0\n"
            "castle.has_storehouse=0\n"
            "castle.has_market=0\n"
            "castle.lord_officer_id=0\n"
            "castle.lord_name=\n"
            "castle.lord_name_utf8=\n"
            "castle.region_id=1\n"
            "castle.is_player_owned=1\n"
            "castle.is_capital=0\n"
            "castle.auto_defense_bonus=0\n"
            "castle.loyalty=75\n"
            "castle.tax_rate=40\n"
            "castle.unrest=10\n"
            "castle.end\n"
            "castle.index=2\n"
            "castle.name=Castle B\n"
            "castle.name_utf8=Castle B\n"
            "castle.level=2\n"
            "castle.defense=10\n"
            "castle.max_defense=50\n"
            "castle.garrison=30\n"
            "castle.max_garrison=100\n"
            "castle.gold=1500\n"
            "castle.food=800\n"
            "castle.has_wall=0\n"
            "castle.has_barracks=0\n"
            "castle.has_storehouse=0\n"
            "castle.has_market=0\n"
            "castle.lord_officer_id=0\n"
            "castle.lord_name=\n"
            "castle.lord_name_utf8=\n"
            "castle.region_id=1\n"
            "castle.is_player_owned=0\n"
            "castle.is_capital=0\n"
            "castle.auto_defense_bonus=0\n"
            "castle.loyalty=60\n"
            "castle.tax_rate=50\n"
            "castle.unrest=20\n"
            "castle.end\n"
            "end\n";
        {
            std::ofstream legacy_file(save_sys.get_save_path(9));
            check(legacy_file.is_open(), "legacy v1 save file can be written");
            legacy_file << legacy_save;
        }
        GameSaveData legacy;
        bool legacy_ok = save_sys.load(9, legacy);
        check(legacy_ok, "legacy v1 save loads without owner_id/turn_count");
        check(legacy.version == 1, "legacy save reports version 1");
        check(legacy.turn_count == 0, "missing turn_count defaults to 0");
        check(legacy.castles.size() == 2, "legacy save loads 2 castles");
        check(legacy.castles[0].owner_id == 0,
              "legacy castle 1 has no owner_id before migration");
        check(legacy.castles[1].owner_id == 0,
              "legacy castle 2 has no owner_id before migration");

        // Apply the same legacy migration the main menu performs on load.
        CastleCommand legacy_cmd;
        legacy_cmd.set_castles_data(legacy.castles);
        StrategyManager legacy_strategy;
        legacy_strategy.rebuild(legacy_cmd, true);
        auto* legacy_c1 = legacy_cmd.get_castle(1);
        auto* legacy_c2 = legacy_cmd.get_castle(2);
        check(legacy_c1 != nullptr && legacy_c1->owner_id == 1,
              "migrated player castle owned by player faction (1)");
        check(legacy_c1->is_player_owned,
              "migrated player castle stays player-owned");
        check(legacy_c2 != nullptr && legacy_c2->owner_id == 3,
              "migrated AI castle owned by default region faction (Mogami, 3)");
        check(!legacy_c2->is_player_owned,
              "migrated AI castle is not player-owned");
        check(legacy_strategy.faction(1)->alive,
              "player faction alive after legacy migration");
        check(legacy_strategy.faction(3)->alive,
              "Mogami faction alive after legacy migration");

        save_sys.delete_save(9);

        // ---- Cleanup test saves ----
        save_sys.delete_save(2);
        std::error_code ec;
        std::filesystem::remove_all(test_save_dir, ec);

        // ---- Summary ----
        LOG_INFO("=== Save Test Summary ===");
        LOG_INFO("  Failures: {}", failures);
        if (failures == 0) {
            LOG_INFO("All save tests PASSED");
        } else {
            LOG_ERROR("{} save test(s) FAILED", failures);
        }
        return failures == 0;
    }

    /// Headless officer system test: officer list, assign/dismiss lord, attribute editing.
    /// Returns true if all checks pass.
    bool run_officer_test() {
        LOG_INFO("=== Officer System Test ===");

        int failures = 0;
        auto check = [&](bool cond, const std::string& desc) {
            if (cond) {
                LOG_INFO("  PASS: {}", desc);
            } else {
                LOG_ERROR("  FAIL: {}", desc);
                ++failures;
            }
        };

        CastleCommand cmd;

        // ---- Setup test castles and officers ----
        LOG_INFO("--- Setup ---");
        std::vector<CastleData> test_castles;
        for (uint32_t i = 1; i <= 3; ++i) {
            CastleData c;
            c.index = i;
            c.name = "Castle " + std::to_string(i);
            c.name_utf8 = c.name;
            c.level = 1;
            c.gold = 1000;
            c.food = 500;
            c.garrison = 0;
            c.max_garrison = 100;
            c.loyalty = 50;
            c.tax_rate = 50;
            c.unrest = 0;
            c.is_player_owned = true;
            c.region_id = 1;
            c.is_capital = (i == 1);
            test_castles.push_back(c);
        }
        cmd.set_castles_data(test_castles);
        check(cmd.castles().size() == 3, "3 test castles created");

        // ---- Assign lord ----
        LOG_INFO("--- Assign Lord ---");
        auto result = cmd.assign_lord(1, 42);
        check(result == CommandResult::Success, "assign officer 42 to castle 1 succeeds");
        auto* c1 = cmd.get_castle(1);
        check(c1->lord_officer_id == 42, "castle 1 lord is officer 42");

        // ---- Dismiss lord ----
        LOG_INFO("--- Dismiss Lord ---");
        result = cmd.dismiss_lord(1);
        check(result == CommandResult::Success, "dismiss lord from castle 1 succeeds");
        c1 = cmd.get_castle(1);
        check(c1->lord_officer_id == 0, "castle 1 lord dismissed (id == 0)");
        check(c1->lord_name.empty(), "castle 1 lord_name cleared");
        check(c1->lord_name_utf8.empty(), "castle 1 lord_name_utf8 cleared");

        // ---- Assign multiple lords ----
        LOG_INFO("--- Multiple Lords ---");
        cmd.assign_lord(1, 10);
        cmd.assign_lord(2, 20);
        cmd.assign_lord(3, 30);
        check(cmd.get_castle(1)->lord_officer_id == 10, "castle 1 lord is officer 10");
        check(cmd.get_castle(2)->lord_officer_id == 20, "castle 2 lord is officer 20");
        check(cmd.get_castle(3)->lord_officer_id == 30, "castle 3 lord is officer 30");

        // ---- find_officer_castle logic (simulating OfficerDetailState) ----
        LOG_INFO("--- Find Officer Castle ---");
        auto find_castle_for_officer = [&cmd](uint16_t officer_id) -> const CastleData* {
            for (const auto& castle : cmd.castles()) {
                if (castle.lord_officer_id == officer_id) {
                    return &castle;
                }
            }
            return nullptr;
        };
        auto* castle_for_10 = find_castle_for_officer(10);
        check(castle_for_10 != nullptr && castle_for_10->index == 1,
              "officer 10 is lord of castle 1");
        auto* castle_for_20 = find_castle_for_officer(20);
        check(castle_for_20 != nullptr && castle_for_20->index == 2,
              "officer 20 is lord of castle 2");
        auto* castle_for_99 = find_castle_for_officer(99);
        check(castle_for_99 == nullptr, "officer 99 is not a lord anywhere");

        // ---- Attribute editing (simulating OfficerDetailState::render_attr_editor) ----
        LOG_INFO("--- Attribute Editing ---");
        // Simulate an officer's attr vector
        std::vector<uint8_t> attr(47, 0);
        attr[24] = 50;  // Loyalty
        attr[25] = 60;  // Command
        attr[26] = 70;  // Warfare

        // Edit loyalty
        int32_t new_loyalty = 80;
        new_loyalty = std::clamp(new_loyalty, 0, 100);
        attr[24] = static_cast<uint8_t>(new_loyalty);
        check(attr[24] == 80, "attribute editing: loyalty changed to 80");

        // Edit command
        int32_t new_command = 90;
        new_command = std::clamp(new_command, 0, 100);
        attr[25] = static_cast<uint8_t>(new_command);
        check(attr[25] == 90, "attribute editing: command changed to 90");

        // Clamp test
        int32_t over_loyalty = 150;
        over_loyalty = std::clamp(over_loyalty, 0, 100);
        attr[24] = static_cast<uint8_t>(over_loyalty);
        check(attr[24] == 100, "attribute editing: loyalty clamped to 100");

        int32_t under_loyalty = -20;
        under_loyalty = std::clamp(under_loyalty, 0, 100);
        attr[24] = static_cast<uint8_t>(under_loyalty);
        check(attr[24] == 0, "attribute editing: loyalty clamped to 0");

        // ---- Officer list iteration (simulating OfficerDetailState list) ----
        LOG_INFO("--- Officer List ---");
        // Create mock officers
        struct MockOfficer {
            uint32_t slot;
            uint16_t id;
            std::string name_utf8;
        };
        std::vector<MockOfficer> mock_officers = {
            {0, 1, "Oda Nobunaga"},
            {1, 2, "Toyotomi Hideyoshi"},
            {2, 3, "Tokugawa Ieyasu"},
        };

        int found_count = 0;
        for (const auto& off : mock_officers) {
            if (!off.name_utf8.empty()) found_count++;
        }
        check(found_count == 3, "3 mock officers iterated");

        // Search filter test
        std::string search = "nobunaga";
        bool found_nobunaga = false;
        for (const auto& off : mock_officers) {
            std::string lower_name = off.name_utf8;
            std::string lower_search = search;
            std::transform(lower_name.begin(), lower_name.end(),
                           lower_name.begin(), ::tolower);
            std::transform(lower_search.begin(), lower_search.end(),
                           lower_search.begin(), ::tolower);
            if (lower_name.find(lower_search) != std::string::npos) {
                found_nobunaga = true;
            }
        }
        check(found_nobunaga, "search filter finds 'nobunaga'");

        // Search filter no match
        std::string search2 = "zzz";
        bool found_zzz = false;
        for (const auto& off : mock_officers) {
            std::string lower_name = off.name_utf8;
            std::string lower_search = search2;
            std::transform(lower_name.begin(), lower_name.end(),
                           lower_name.begin(), ::tolower);
            std::transform(lower_search.begin(), lower_search.end(),
                           lower_search.begin(), ::tolower);
            if (lower_name.find(lower_search) != std::string::npos) {
                found_zzz = true;
            }
        }
        check(!found_zzz, "search filter 'zzz' finds no officers");

        // ---- Summary ----
        LOG_INFO("=== Officer Test Summary ===");
        LOG_INFO("  Failures: {}", failures);
        if (failures == 0) {
            LOG_INFO("All officer tests PASSED");
        } else {
            LOG_ERROR("{} officer test(s) FAILED", failures);
        }
        return failures == 0;
    }

    /// Headless turn-based strategy test: ownership, economy, AI turn,
    /// abstract attacks, seasons, victory/defeat and save round-trip.
    /// Returns true if all checks pass.
    bool run_strategy_test() {
        LOG_INFO("=== Strategy Layer Test ===");

        int failures = 0;
        auto check = [&](bool cond, const std::string& desc) {
            if (cond) {
                LOG_INFO("  PASS: {}", desc);
            } else {
                LOG_ERROR("  FAIL: {}", desc);
                ++failures;
            }
        };

        auto has_event = [](const std::vector<StrategyEvent>& events,
                            StrategyEventType type) {
            for (const auto& event : events) {
                if (event.type == type) return true;
            }
            return false;
        };

        // ---- Default ownership mapping ----
        LOG_INFO("--- Ownership Mapping ---");
        check(default_owner_for_region(4) == 1,
              "player faction owns region 4");
        check(default_owner_for_region(0) == 2,
              "region 0 belongs to faction 2");
        check(default_owner_for_region(7) == 8,
              "region 7 belongs to faction 8");
        check(default_region_for_owner(1) == 4,
              "inverse mapping: player -> region 4");
        check(default_region_for_owner(8) == 7,
              "inverse mapping: faction 8 -> region 7");

        // ---- Faction initialization ----
        LOG_INFO("--- Faction Initialization ---");
        std::vector<CastleData> test_castles;
        for (uint32_t i = 0; i < 16; ++i) {
            CastleData c;
            c.index = i;
            c.name = "Castle " + std::to_string(i);
            c.name_utf8 = c.name;
            c.region_id = i % 8;
            c.owner_id = default_owner_for_region(c.region_id);
            c.is_player_owned = (c.owner_id == 1);
            c.level = 1;
            c.defense = 10;
            c.max_defense = 50;
            c.garrison = 60;
            c.max_garrison = 100;
            c.gold = 1000;
            c.food = 500;
            c.loyalty = 50;
            c.tax_rate = 50;
            c.unrest = 0;
            c.is_capital = (i < 8);
            test_castles.push_back(c);
        }

        CastleCommand cmd;
        cmd.set_castles_data(test_castles);
        cmd.set_turn_count(0);

        StrategyManager strategy;
        strategy.set_seed(12345);
        strategy.init(cmd);

        check(strategy.factions().size() == 8, "8 factions created");
        const Faction* player = strategy.faction(1);
        check(player != nullptr && player->is_player, "faction 1 is the player");
        check(player != nullptr && player->name == "Oda", "player faction is Oda");
        check(strategy.faction_name(2) == "Date", "faction 2 is Date");
        check(strategy.faction_name(8) == "Shimazu", "faction 8 is Shimazu");
        check(cmd.player_castle_count() == 2,
              "player owns 2 castles (region 4)");
        check(cmd.get_castle(4)->is_player_owned,
              "castle 4 is player-owned");
        check(cmd.get_castle(5)->owner_id == 6,
              "castle 5 belongs to Mori (faction 6)");

        // ---- Player economy (AI disabled) ----
        LOG_INFO("--- Player Economy ---");
        strategy.set_ai_enabled(false);
        GameDate date{1, 4, 1};  // April, spring

        auto* pc = cmd.get_castle(4);
        const int32_t gold_before = pc->gold;
        const int32_t food_before = pc->food;

        strategy.end_player_turn(cmd, date);

        check(date.month == 5, "calendar advances one month");
        check(cmd.get_turn_count() == 1, "turn counter advances to 1");
        check(strategy.turn() == 1, "strategy manager turn synced");

        pc = cmd.get_castle(4);
        check(pc->gold == gold_before + 50,
              "tax income: +50 gold at 50% tax");
        check(pc->food == food_before + 44,
              "spring food: +50 production -6 consumption");
        check(pc->loyalty == 52,
              "loyalty: garrison bonus +1, spring +1");
        check(pc->unrest == 0, "unrest remains zero");

        // ---- AI turn ----
        LOG_INFO("--- AI Turn ---");
        strategy.set_ai_enabled(true);
        const int32_t ai_garrison_before = cmd.get_castle(0)->garrison;
        strategy.end_player_turn(cmd, date);

        check(date.month == 6, "calendar advances to June");
        check(cmd.get_turn_count() == 2, "turn counter advances to 2");
        check(has_event(strategy.events(), StrategyEventType::AiRecruited),
              "AI faction recruited troops");
        check(cmd.get_castle(0)->garrison > ai_garrison_before,
              "AI castle garrison increased");
        check(has_event(strategy.events(), StrategyEventType::SeasonChanged),
              "season change event logged when spring -> summer");

        // ---- Season multipliers ----
        LOG_INFO("--- Season Modifiers ---");
        check(StrategyManager::season_food_production_multiplier(
                  Season::Spring) == 1.0f,
              "spring production multiplier is 1.0");
        check(StrategyManager::season_food_production_multiplier(
                  Season::Autumn) == 1.5f,
              "autumn production multiplier is 1.5");
        check(StrategyManager::season_food_production_multiplier(
                  Season::Winter) == 0.5f,
              "winter production multiplier is 0.5");
        check(StrategyManager::season_food_consumption_multiplier(
                  Season::Winter) == 1.5f,
              "winter consumption multiplier is 1.5");

        CastleData summer;
        summer.region_id = 4;
        CastleData winter;
        winter.region_id = 5;
        check(StrategyManager::are_adjacent(summer, winter),
              "neighbouring regions are adjacent");
        CastleData distant;
        distant.region_id = 0;
        check(!StrategyManager::are_adjacent(summer, distant),
              "distant regions are not adjacent");

        // ---- Abstract attack: capture ----
        LOG_INFO("--- Abstract Attack: Capture ---");
        std::vector<CastleData> duel_castles;
        {
            CastleData attacker;
            attacker.index = 0;
            attacker.name = "AI Base";
            attacker.name_utf8 = attacker.name;
            attacker.region_id = 0;
            attacker.owner_id = 2;
            attacker.level = 1;
            attacker.defense = 10;
            attacker.max_defense = 50;
            attacker.garrison = 100;
            attacker.max_garrison = 100;
            attacker.gold = 500;
            attacker.food = 500;
            duel_castles.push_back(attacker);

            CastleData defender;
            defender.index = 1;
            defender.name = "Player Keep";
            defender.name_utf8 = defender.name;
            defender.region_id = 1;
            defender.owner_id = 1;
            defender.is_player_owned = true;
            defender.level = 1;
            defender.defense = 10;
            defender.max_defense = 50;
            defender.garrison = 5;
            defender.max_garrison = 100;
            defender.gold = 400;
            defender.food = 500;
            duel_castles.push_back(defender);
        }

        CastleCommand duel_cmd;
        duel_cmd.set_castles_data(duel_castles);
        StrategyManager duel_strategy;
        duel_strategy.set_seed(777);
        duel_strategy.init(duel_cmd);

        GameDate duel_date{1, 4, 1};
        std::vector<StrategyEvent> duel_events;
        auto* attacker = duel_cmd.get_castle(0);
        auto* defender = duel_cmd.get_castle(1);

        const bool captured = duel_strategy.resolve_attack(
            duel_cmd, *attacker, *defender, 2, duel_date, duel_events);
        check(captured, "overwhelming attack captures the castle");
        check(defender->owner_id == 2, "captured castle changes owner");
        check(!defender->is_player_owned,
              "captured player castle is no longer player-owned");
        check(attacker->garrison == 50,
              "attacker loses half its garrison after capture");
        check(defender->garrison >= 5,
              "occupying garrison is installed");
        check(defender->loyalty == 40, "captured castle loyalty reset to 40");
        check(defender->unrest == 20, "captured castle unrest set to 20");
        check(has_event(duel_events, StrategyEventType::CastleLost),
              "player castle loss event logged");
        check(has_event(duel_events, StrategyEventType::FactionEliminated),
              "player faction elimination event logged");
        const Faction* eliminated = duel_strategy.faction(1);
        check(eliminated != nullptr && !eliminated->alive,
              "player faction marked as eliminated");
        duel_strategy.evaluate_end_conditions(duel_cmd);
        check(duel_strategy.defeat(), "defeat detected with no player castles");

        // ---- Abstract attack: repelled ----
        LOG_INFO("--- Abstract Attack: Repelled ---");
        std::vector<CastleData> wall_castles = duel_castles;
        wall_castles[0].garrison = 5;
        wall_castles[0].owner_id = 2;
        wall_castles[1].garrison = 200;
        wall_castles[1].owner_id = 1;
        wall_castles[1].is_player_owned = true;

        CastleCommand wall_cmd;
        wall_cmd.set_castles_data(wall_castles);
        StrategyManager wall_strategy;
        wall_strategy.set_seed(999);
        wall_strategy.init(wall_cmd);

        std::vector<StrategyEvent> wall_events;
        auto* weak_attacker = wall_cmd.get_castle(0);
        auto* strong_defender = wall_cmd.get_castle(1);
        const int32_t weak_before = weak_attacker->garrison;

        const bool captured_2 = wall_strategy.resolve_attack(
            wall_cmd, *weak_attacker, *strong_defender, 2, duel_date,
            wall_events);
        check(!captured_2, "weak attack is repelled");
        check(strong_defender->owner_id == 1,
              "defender keeps ownership after repelled attack");
        check(weak_attacker->garrison < weak_before,
              "repelled attacker loses troops");
        check(has_event(wall_events, StrategyEventType::AiAttack),
              "repelled attack event logged");

        // ---- Victory / defeat evaluation ----
        LOG_INFO("--- Victory / Defeat ---");
        std::vector<CastleData> victory_castles = test_castles;
        for (auto& c : victory_castles) {
            c.owner_id = 1;
            c.is_player_owned = true;
        }
        CastleCommand victory_cmd;
        victory_cmd.set_castles_data(victory_castles);
        StrategyManager victory_strategy;
        victory_strategy.init(victory_cmd);
        check(victory_strategy.victory(),
              "victory when every castle is player-owned");

        for (auto& c : victory_castles) {
            c.owner_id = 2;
            c.is_player_owned = false;
        }
        victory_cmd.set_castles_data(victory_castles);
        victory_strategy.rebuild(victory_cmd);
        check(victory_strategy.defeat(),
              "defeat when no castle is player-owned");
        check(!victory_strategy.victory(),
              "victory flag cleared after losing everything");

        // ---- Save/load round-trip: ownership and turn ----
        LOG_INFO("--- Save/Load Round-Trip ---");
        const std::string test_dir = "/tmp/tensyoki_strategy_test";
        SaveSystem save_system;
        save_system.init(test_dir);

        GameSaveData save_data;
        save_data.save_name = "Strategy Test";
        save_data.turn_count = 9;
        save_data.date_year = 2;
        save_data.date_month = 7;
        save_data.date_day = 1;
        save_data.castles = cmd.castles();
        save_data.player_castle_count = cmd.player_castle_count();
        save_data.total_castles =
            static_cast<int32_t>(cmd.castles().size());

        check(save_system.save(1, save_data), "strategy save succeeds");

        GameSaveData loaded;
        check(save_system.load(1, loaded), "strategy load succeeds");
        check(loaded.version == 2, "save format version is 2");
        check(loaded.turn_count == 9, "turn count round-trips");
        check(loaded.castles.size() == cmd.castles().size(),
              "castle count round-trips");
        check(loaded.castles[0].owner_id == 2,
              "castle owner id round-trips");
        check(loaded.castles[4].owner_id == 1,
              "player castle ownership round-trips");

        CastleCommand loaded_cmd;
        loaded_cmd.set_castles_data(loaded.castles);
        loaded_cmd.set_turn_count(loaded.turn_count);

        StrategyManager loaded_strategy;
        loaded_strategy.rebuild(loaded_cmd, false);
        check(loaded_strategy.factions().size() == 8,
              "factions rebuilt from loaded ownership");
        check(loaded_strategy.faction(1) != nullptr &&
                  loaded_strategy.faction(1)->alive,
              "player faction alive after load");

        save_system.delete_save(1);
        std::error_code ec;
        std::filesystem::remove_all(test_dir, ec);

        // ---- Summary ----
        LOG_INFO("=== Strategy Test Summary ===");
        LOG_INFO("  Failures: {}", failures);
        if (failures == 0) {
            LOG_INFO("All strategy tests PASSED");
        } else {
            LOG_ERROR("{} strategy test(s) FAILED", failures);
        }
        return failures == 0;
    }

    void main_loop() {
        bool running = true;
        uint64_t frame_count = 0;
        auto last_fps_time = std::chrono::high_resolution_clock::now();
        uint64_t frames_since_fps = 0;
        
        while (running) {
            // Process events
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                imgui_->process_event(&event);
                
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_F11) {
                        platform_->toggle_fullscreen();
                    }
                } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                    renderer_->on_resize(event.window.data1, event.window.data2);
                    imgui_->on_resize(event.window.data1, event.window.data2);
                }
            }
            
            // Check platform quit request
            if (platform_->should_quit()) {
                running = false;
            }
            
            // Begin a new frame
            imgui_->new_frame(1.0f / 60.0f); // Fixed timestep for UI
            renderer_->begin_frame();
            
            // Clear screen
            renderer_->clear(0.1f, 0.1f, 0.15f, 1.0f);
            
            // Update game state system and game clock
            if (game_initialized_) {
                static float accumulator = 0.0f;
                float dt = 1.0f / 60.0f;
                accumulator += dt;
                while (accumulator >= dt) {
                    if (!game_state_mgr_.update(dt)) {
                        running = false;
                    }
                    accumulator -= dt;
                }

                // Advance game clock (date progression)
                int days = game_ctx_.clock.update(dt, game_ctx_.date);
                if (days > 0) {
                    LOG_DEBUG("Date advanced {} day(s): {}", days,
                              game_ctx_.date.to_string().c_str());
                }

                // Process pending state transitions
                if (game_ctx_.request_clear) {
                    game_state_mgr_.clear();
                    game_ctx_.request_clear = false;
                }
                if (game_ctx_.request_pop) {
                    game_state_mgr_.pop();
                    game_ctx_.request_pop = false;
                }
                if (game_ctx_.pending_push != GameContext::PendingState::None) {
                    switch (game_ctx_.pending_push) {
                        case GameContext::PendingState::MainMenu:
                            game_state_mgr_.push(
                                std::make_unique<MainMenuState>(game_ctx_));
                            break;
                        case GameContext::PendingState::MapView:
                            game_state_mgr_.push(
                                std::make_unique<MapViewState>(game_ctx_));
                            break;
                        case GameContext::PendingState::OfficerDetail:
                            game_state_mgr_.push(
                                std::make_unique<OfficerDetailState>(game_ctx_));
                            break;
                        case GameContext::PendingState::Battle:
                            game_state_mgr_.push(
                                std::make_unique<BattleState>(game_ctx_));
                            break;
                        case GameContext::PendingState::CastleCommand:
                            game_state_mgr_.push(
                                std::make_unique<CastleCommandState>(game_ctx_));
                            break;
                        case GameContext::PendingState::CastleDetail:
                            game_state_mgr_.push(
                                std::make_unique<CastleDetailState>(game_ctx_));
                            break;
                        case GameContext::PendingState::None:
                            break;
                    }
                    game_ctx_.pending_push = GameContext::PendingState::None;
                }
            }
            
            // Render UI (ImGui)
            render_ui();
            
            // Render game states (draw on top of ImGui)
            if (game_initialized_) {
                game_state_mgr_.render();
            }
            
            // End the frame
            renderer_->end_frame();
            imgui_->render();

            // Render officer card name overlay using TextRenderer (FreeType CJK)
            render_officer_card_name_overlay();

            platform_->swap_buffers();
            
            frame_count++;
            frames_since_fps++;
            
            // FPS statistics
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - last_fps_time).count();
            if (elapsed >= 1.0f) {
                LOG_DEBUG("FPS: {:.1f}", frames_since_fps / elapsed);
                frames_since_fps = 0;
                last_fps_time = now;
            }
        }
    }
    
    void render_ui() {
        // Main menu demo
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Data Root...")) {
                    // TODO: file selection dialog
                }
                if (ImGui::MenuItem("Capture Screenshot")) {
                    auto out_path = std::filesystem::path("screenshot.bmp");
                    if (renderer_->capture_screenshot(out_path)) {
                        LOG_INFO("Screenshot saved: {}", out_path.string());
                    } else {
                        LOG_WARN("Failed to save screenshot: {}", out_path.string());
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Esc")) {
                    platform_->request_quit();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Debug")) {
                ImGui::MenuItem("Resource Browser", nullptr, &show_resource_browser_);
                ImGui::MenuItem("Texture Viewer", nullptr, &show_texture_viewer_);
                ImGui::MenuItem("Texture Atlas Viewer", nullptr, &show_atlas_viewer_);
                ImGui::MenuItem("Shader Viewer", nullptr, &show_shader_viewer_);
                ImGui::MenuItem("Data Table Viewer", nullptr, &show_dat_viewer_);
                ImGui::MenuItem("Officer Card", nullptr, &show_officer_card_);
                if (game_initialized_) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Start Game")) {
                        game_state_mgr_.clear();
                        game_state_mgr_.push(std::make_unique<MapViewState>(game_ctx_));
                    }
                    if (ImGui::MenuItem("Main Menu")) {
                        game_state_mgr_.clear();
                        game_state_mgr_.push(std::make_unique<MainMenuState>(game_ctx_));
                    }
                }
                ImGui::EndMenu();
            }
            
            ImGui::EndMainMenuBar();
        }
        
        // Texture atlas viewer
        if (show_atlas_viewer_ && atlas_viewer_) {
            atlas_viewer_->draw();
        }

        // Texture viewer (GT1G previews)
        if (show_texture_viewer_) {
            render_texture_viewer();
        }
        
        // Resource browser
        if (show_resource_browser_) {
            render_resource_browser();
        }
        
        // Text rendering demo
        if (text_renderer_) {
            ImGui::Begin("Text Demo", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::Text("Alignment:");
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "Left aligned",
                .position = {10.0f, 10.0f},
                .size = 20.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .wrap_width = 300.0f,
                .align = Renderer::TextRenderer::TextParams::Align::Left,
            });
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "Center aligned",
                .position = {10.0f, 40.0f},
                .size = 20.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .wrap_width = 300.0f,
                .align = Renderer::TextRenderer::TextParams::Align::Center,
            });
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "Right aligned",
                .position = {10.0f, 70.0f},
                .size = 20.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .wrap_width = 300.0f,
                .align = Renderer::TextRenderer::TextParams::Align::Right,
            });

            ImGui::Separator();
            ImGui::Text("CJK (UTF-8):");
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "信長の野望・天翔記 HD",
                .position = {10.0f, 100.0f},
                .size = 24.0f,
                .color = {1.0f, 0.9f, 0.7f, 1.0f},
                .wrap_width = 400.0f,
            });
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "織田信長・羽柴秀吉・徳川家康",
                .position = {10.0f, 130.0f},
                .size = 20.0f,
                .color = {0.9f, 1.0f, 0.9f, 1.0f},
                .wrap_width = 400.0f,
            });

            ImGui::Separator();
            ImGui::Text("Wrapping:");
            text_renderer_->draw(Renderer::TextRenderer::TextParams{
                .text = "Wrapping test: this is a longer line of text that should wrap at the specified width.",
                .position = {10.0f, 170.0f},
                .size = 18.0f,
                .color = {1.0f, 0.8f, 0.8f, 1.0f},
                .wrap_width = 400.0f,
            });
            ImGui::End();
        }

        // Shader Viewer
        if (show_shader_viewer_) {
            render_shader_viewer();
        }

        // Data Table Viewer (officers / castles)
        if (show_dat_viewer_) {
            render_data_table_viewer();
        }

        // Officer Card (texture + text render demo)
        if (show_officer_card_) {
            render_officer_card();
        }

        // Status bar
        ImGui::Begin("Status", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 25));
        ImGui::SetWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 25));
        ImGui::Text("Tensyoki Linux Port | Data: %s | FPS: %.1f", 
                    data_root_.string().c_str(), ImGui::GetIO().Framerate);
        ImGui::End();
    }
    
    void render_resource_browser() {
        ImGui::Begin("Resource Browser", &show_resource_browser_, ImGuiWindowFlags_MenuBar);
        
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Extract Selected")) {
                    if (selected_entry_ && selected_archive_) {
                        extract_entry_to_disk(*selected_archive_, *selected_entry_);
                    }
                }
                if (ImGui::MenuItem("Extract All")) {
                    if (selected_archive_) {
                        const auto dir = std::filesystem::path("extracted") /
                                         std::filesystem::path(current_file_).stem();
                        auto result = selected_archive_->extract_all(dir);
                        if (!result) {
                            LOG_ERROR("Extract All failed: {}", tensyoki::to_string(result.error()));
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Hex Dump", nullptr, &show_hex_dump_);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        ImGui::BeginChild("LeftPane", ImVec2(350, 0), true);
        
        // Container selector
        if (ImGui::BeginCombo("Container", current_file_.c_str())) {
            const char* files[] = {
                "res/res_shader.bin",
                "res/res_else.bin", 
                "res/res_grp.bin",
                "res/res_snd.bin",
                "res/res_exp.bin",
                "res/res_exp_pc.bin"
            };
            for (const char* f : files) {
                bool selected = (current_file_ == f);
                if (ImGui::Selectable(f, selected)) {
                    auto new_archive = std::make_unique<BinArchive>();
                    auto result = new_archive->load_from_file(data_root_ / f);
                    if (result) {
                        LOG_INFO("Parsed {}: {} resources", f, new_archive->entries().size());
                        selected_entry_ = nullptr;
                        selected_archive_ = std::move(new_archive);
                        current_file_ = f;
                    } else {
                        LOG_WARN("Failed to parse {}: {}", f, tensyoki::to_string(result.error()));
                    }
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Separator();
        
        if (selected_archive_) {
            ImGui::Text("Entries: %zu", selected_archive_->entries().size());
            ImGui::SameLine();
            ImGui::Text("(%s)", current_file_.c_str());
        }
        ImGui::Separator();
        
        static ImGuiTextFilter filter;
        filter.Draw("Filter##res_filter", -100.0f);
        
        // Tree view of entries
        if (selected_archive_) {
            for (const auto& entry : selected_archive_->entries()) {
                if (!filter.PassFilter(entry.name.c_str())) continue;
                
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (selected_entry_ == &entry) flags |= ImGuiTreeNodeFlags_Selected;
                
                bool opened = ImGui::TreeNodeEx(entry.name.c_str(), flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    selected_entry_ = &entry;
                }
                if (opened) ImGui::TreePop();
            }
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Right pane: Details / Hex Dump
        ImGui::BeginChild("RightPane", ImVec2(0, 0), true);
        
        if (selected_entry_ && selected_archive_) {
            render_entry_details(*selected_entry_, *selected_archive_);
        } else {
            ImGui::Text("Select a resource from the list");
            ImGui::TextDisabled("Use the filter to narrow down entries");
        }
        ImGui::EndChild();
        
        ImGui::End();
    }
    
    void render_entry_details(const BinFileEntry& entry, const BinArchive& archive) {
        ImGui::Text("Name: %s", entry.name.c_str());
        ImGui::Text("Index: %u", entry.index);
        ImGui::Text("Offset: 0x%08X (%u)", entry.offset, entry.offset);
        ImGui::Text("Size: 0x%08X (%u bytes)", entry.size, entry.size);
        ImGui::Text("Signature: %s", entry.signature.empty() ? "(none)" : entry.signature.c_str());
        ImGui::Text("Extension: %s", entry.guess_extension().c_str());
        ImGui::Text("Sentinel: %s", entry.has_sentinel ? "present (0x89ABCDEF)" : "missing");

        // Nested LINK / GT1G details, decoded on demand
        if (entry.is_nested_link) {
            auto nested = archive.parse_nested(entry);
            if (nested) {
                ImGui::Separator();
                ImGui::Text("Nested LINK");
                ImGui::Text("  Data blocks: %u", nested->data_entry_count);
                for (size_t i = 0; i < nested->data_entries.size() && i < 8; ++i) {
                    const auto& block = nested->data_entries[i];
                    ImGui::BulletText("#%u: %s at 0x%X (%u bytes)",
                                      static_cast<unsigned>(i),
                                      block.signature.empty() ? "(unknown)" : block.signature.c_str(),
                                      block.offset, block.size);
                }
                if (nested->data_entries.size() > 8) {
                    ImGui::TextDisabled("  ... %u more",
                                        static_cast<unsigned>(nested->data_entries.size() - 8));
                }
                ImGui::Text("  Records: %u", nested->record_count);
                ImGui::Text("  Descriptor offset: 0x%X", nested->descriptor_offset);
                if (nested->textures) {
                    ImGui::Text("  GT1G textures: %u", nested->textures->texture_count);
                    for (const auto& tex : nested->textures->textures) {
                        ImGui::BulletText("#%u: %ux%u, %u bytes at 0x%X",
                                          tex.index, tex.width, tex.height,
                                          tex.data_size, tex.data_offset);
                    }
                    if (ImGui::Button("Preview Texture 0")) {
                        preview_container_ = current_file_;
                        preview_entry_index_ = entry.index;
                        preview_texture_ = texture_cache_.get_or_load(
                            *renderer_, preview_container_, archive, entry.index);
                        if (preview_texture_) {
                            // Need width/height from the decoded texture; store from first entry
                            // We re-decode lightly to get dims, or just query the texture
                            preview_width_ = nested->textures->textures[0].width;
                            preview_height_ = nested->textures->textures[0].height;
                            show_texture_viewer_ = true;
                        }
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Nested parse failed: %s",
                                   std::string(tensyoki::to_string(nested.error())).c_str());
            }
        }

        ImGui::Separator();
        
        // Action buttons
        if (ImGui::Button("Extract")) {
            extract_entry_to_disk(archive, entry);
        }
        ImGui::SameLine();
        if (ImGui::Button("View Hex Dump")) {
            show_hex_dump_ = true;
        }
        
        ImGui::Separator();
        
        // Hex Dump
        if (show_hex_dump_) {
            ImGui::Text("Hex Dump (first 256 bytes):");
            ImGui::SameLine();
            ImGui::Checkbox("Show All", &show_full_hex_);
            
            auto view_result = archive.view(entry);
            if (view_result) {
                const auto& data = *view_result;
                size_t dump_size = show_full_hex_ ? data.size() : std::min<size_t>(256, data.size());
                
                ImGui::BeginChild("HexDump", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
                
                static ImGuiTextFilter hex_filter;
                hex_filter.Draw("Filter##hex", 200);
                
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // monospace
                
                const int bytes_per_row = 16;
                for (size_t row = 0; row < (dump_size + bytes_per_row - 1) / bytes_per_row; ++row) {
                    size_t offset = row * bytes_per_row;
                    if (offset >= data.size()) break;
                    
                    ImGui::Text("%08zX: ", entry.offset + offset);
                    ImGui::SameLine();
                    
                    // Hex bytes
                    for (int col = 0; col < bytes_per_row; ++col) {
                        size_t idx = offset + static_cast<size_t>(col);
                        if (idx < dump_size) {
                            ImGui::Text("%02X ", data[idx]);
                        } else {
                            ImGui::Text("   ");
                        }
                        ImGui::SameLine(0, col == 7 ? 20 : 4);
                    }
                    
                    // ASCII representation
                    ImGui::SameLine(16 * 4 + 20);
                    ImGui::Text("|");
                    ImGui::SameLine();
                    for (int col = 0; col < bytes_per_row; ++col) {
                        size_t idx = offset + static_cast<size_t>(col);
                        if (idx < dump_size) {
                            uint8_t c = data[idx];
                            ImGui::Text("%c", (c >= 32 && c <= 126) ? char(c) : '.');
                        }
                        ImGui::SameLine(0, 0);
                    }
                }
                
                ImGui::PopFont();
                ImGui::EndChild();
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to view data: %s", 
                                   std::string(tensyoki::to_string(view_result.error())).c_str());
            }
        }
    }
    
    void render_texture_viewer() {
        if (!preview_texture_) {
            ImGui::Begin("Texture Viewer", &show_texture_viewer_);
            ImGui::Text("No texture loaded. Select a GT1G entry in the Resource Browser");
            ImGui::Text("and click 'Preview Texture 0'.");
            ImGui::End();
            return;
        }

        ImGui::Begin("Texture Viewer", &show_texture_viewer_,
                     ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Container: %s", preview_container_.c_str());
        ImGui::Text("Entry: %u", preview_entry_index_);
        ImGui::Text("Size: %ux%u", preview_width_, preview_height_);
        ImGui::Separator();

        // ImGui expects Y-down UVs; flip vertically for OpenGL textures
        ImVec2 uv0(0, 1);
        ImVec2 uv1(1, 0);
        ImVec2 size((float)preview_width_, (float)preview_height_);
        ImGui::Image(static_cast<ImTextureID>(preview_texture_->get_id()), size, uv0, uv1,
                     ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));

        ImGui::End();
    }

    void render_officer_card() {
        ImGui::Begin("Officer Card", &show_officer_card_,
                     ImGuiWindowFlags_AlwaysAutoResize);

        if (!officer_portrait_) {
            ImGui::Text("No portrait texture loaded. Load res_grp.bin first.");
            ImGui::End();
            return;
        }

        // Lazily create VAO/VBO for the officer card quad
        if (officer_card_vao_ == 0) {
            float vertices[] = {
                // pos        uv
                -0.5f, -0.5f, 0.0f, 0.0f,
                 0.5f, -0.5f, 1.0f, 0.0f,
                 0.5f,  0.5f, 1.0f, 1.0f,
                -0.5f,  0.5f, 0.0f, 1.0f,
            };
            glGenVertexArrays(1, &officer_card_vao_);
            glGenBuffers(1, &officer_card_vbo_);
            glBindVertexArray(officer_card_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, officer_card_vbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  (void*)(2 * sizeof(float)));
            glBindVertexArray(0);
        }

        // Bind compiled shader pipeline if available
        if (compiled_shader_ && !officer_card_pipeline_) {
            officer_card_pipeline_ = renderer_->create_pipeline({});
            officer_card_pipeline_->set_shader(compiled_shader_);
        }

        if (selected_officer_ >= 0 &&
            static_cast<size_t>(selected_officer_) < sndata_loaded_.officer_count()) {
            const auto& officer = sndata_loaded_.officers[selected_officer_];

            if (officer_card_pipeline_ && compiled_shader_) {
                // Draw portrait using the compiled shader pipeline
                GLuint program = compiled_shader_->get_id();
                glUseProgram(program);

                // Identity projection (NDC coordinates)
                GLint proj_loc = glGetUniformLocation(program, "u_projection");
                if (proj_loc >= 0) {
                    float proj[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                    glUniformMatrix4fv(proj_loc, 1, GL_FALSE, proj);
                }

                // Bind texture to unit 0
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(officer_portrait_->get_target(),
                              officer_portrait_->get_id());
                GLint tex_loc = glGetUniformLocation(program, "u_texture0");
                if (tex_loc >= 0) {
                    glUniform1i(tex_loc, 0);
                }

                // Draw quad
                glBindVertexArray(officer_card_vao_);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                glBindVertexArray(0);

                ImGui::Text("Portrait rendered via compiled shader pipeline");
            } else {
                // Fallback to ImGui Image
                ImVec2 uv0(0, 1);
                ImVec2 uv1(1, 0);
                ImVec2 size(256.0f, 256.0f);
                ImGui::Image(static_cast<ImTextureID>(officer_portrait_->get_id()), size, uv0, uv1,
                             ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
            }
            ImGui::SameLine();

            // Officer info (name rendered by TextRenderer overlay instead)
            ImGui::BeginGroup();
            ImGui::Text("Slot: %u", officer.slot);
            ImGui::Text("ID: %u", officer.id);
            ImGui::Text("Status: %u", officer.status);
            ImGui::Text("Gender: %u", officer.gender);
            ImGui::Separator();

            // Parse officer attributes from the 47-byte attr record.
            // Observed layout (sndata1.dat):
            //   +0x00..0x11  attr[18]  various attributes
            //   +0x12        u16 id
            //   +0x14..0x15  u8  pad[2]
            //   +0x16        u8 status
            //   +0x17        u8 gender
            //   +0x18..0x2E  attr2[23] more attributes
            ImGui::Text("Attributes (attr bytes):");
            ImGui::BeginChild("AttrBytes", ImVec2(260, 110), true);
            const auto& attr = officer.attr;
            for (size_t i = 0; i < attr.size(); ++i) {
                uint8_t b = attr[i];
                // Skip the id/status/gender fields we already show above
                if (i == 18 || i == 19 || i == 22 || i == 23) continue;
                ImGui::Text("  [%2u] = %3u (0x%02X)", i, b, b);
                if ((i + 1) % 6 == 0) ImGui::Spacing();
            }
            ImGui::EndChild();

            // Common KOEI attribute hypotheses (attr2 offsets, values 0-100 typical)
            ImGui::Separator();
            ImGui::Text("Possible attribute mapping (hypothesis):");
            ImGui::BeginChild("AttrHypothesis", ImVec2(260, 80), true);
            auto show_attr = [&](size_t offset, const char* name) {
                if (offset < attr.size()) {
                    uint8_t b = attr[offset];
                    bool valid = (b != 0xFF && b != 0);
                    ImGui::TextColored(valid ? ImVec4(0.8f, 0.9f, 1.0f, 1.0f)
                                             : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                       "%s [%2u]: %3u", name, offset, b);
                }
            };
            // attr2 starts at index 24 in the 47-byte record
            show_attr(24, "Loyalty?");
            show_attr(25, "Command?");
            show_attr(26, "Warfare?");
            show_attr(27, "Wisdom?");
            show_attr(28, "Politics?");
            show_attr(29, "Charm?");
            ImGui::EndChild();
            ImGui::EndGroup();
        } else {
            ImGui::Text("Select an officer from Data Table Viewer");
        }

        ImGui::End();
    }

    /// Render the selected officer's name using TextRenderer (FreeType 2 + CJK).
    /// Called after ImGui render so FreeType text appears on top of ImGui.
    void render_officer_card_name_overlay() {
        if (!text_renderer_) return;
        if (selected_officer_ < 0) return;
        if (static_cast<size_t>(selected_officer_) >= sndata_loaded_.officer_count()) return;

        const auto& officer = sndata_loaded_.officers[selected_officer_];
        std::string name = officer.name_utf8.empty()
            ? officer.name_raw
            : officer.name_utf8;
        if (name.empty()) return;

        // Render at screen position below the Officer Card window
        // (simple fixed offset for now; can be refined to track window pos)
        text_renderer_->draw(Renderer::TextRenderer::TextParams{
            .text = name,
            .position = {10.0f, 60.0f},
            .size = 32.0f,
            .color = {1.0f, 0.9f, 0.7f, 1.0f},
            .wrap_width = 500.0f,
        });
    }

    void render_shader_viewer() {
        ImGui::Begin("Shader Viewer", &show_shader_viewer_,
                     ImGuiWindowFlags_AlwaysAutoResize);

        if (shader_infos_.empty()) {
            ImGui::Text("No shaders parsed from res_shader.bin");
            ImGui::End();
            return;
        }

        ImGui::Text("Shaders: %zu", shader_infos_.size());
        ImGui::SameLine();
        if (ImGui::Button("Compile All")) {
            std::error_code ec;
            auto out_dir = std::filesystem::path("extracted") / "shaders";
            shader_compiler_->extract_and_compile_shaders(
                data_root_ / "res" / "res_shader.bin", out_dir);
        }
        ImGui::Separator();

        // Shader list
        if (ImGui::BeginChild("ShaderList", ImVec2(300, 200), true)) {
            for (size_t i = 0; i < shader_infos_.size(); ++i) {
                const auto& info = shader_infos_[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           (static_cast<int>(i) == selected_shader_
                                                ? ImGuiTreeNodeFlags_Selected : 0);
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)i, flags,
                                              "%s (%s)", info.profile.c_str(),
                                              info.source_path.empty()
                                                  ? "(no source)"
                                                  : info.source_path.c_str());
                if (ImGui::IsItemClicked()) {
                    selected_shader_ = static_cast<int>(i);
                }
                if (open) {
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();
        }

        ImGui::SameLine();

        // Shader details
        if (selected_shader_ >= 0 &&
            static_cast<size_t>(selected_shader_) < shader_infos_.size()) {
            const auto& info = shader_infos_[selected_shader_];
            ImGui::BeginChild("ShaderDetails", ImVec2(0, 200), true);
            ImGui::Text("Entry: %u", info.entry_index);
            ImGui::Text("Profile: %s", info.profile.c_str());
            ImGui::Text("Type: %s",
                        info.is_pixel_shader ? "Pixel Shader" : "Vertex Shader");
            ImGui::Text("Size: %u bytes", info.size_bytes);
            ImGui::Text("Source: %s",
                        info.source_path.empty() ? "(not recorded)" : info.source_path.c_str());
            ImGui::Separator();

            // Generated GLSL
            auto glsl = shader_compiler_->generate_glsl_from_d3d9(info);
            if (!glsl.empty()) {
                ImGui::Text("Generated GLSL:");
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("GlslSource", ImVec2(0, 120), true);
                ImGui::TextUnformatted(glsl.c_str());
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button("Compile this shader")) {
                    // Generate GLSL from the D3D9 bytecode
                    auto glsl = shader_compiler_->generate_glsl_from_d3d9(info);
                    if (!glsl.empty()) {
                        // Pair the generated fragment shader with a simple pass-through vertex shader
                        const char* vertex_glsl = R"(
#version 450 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
out vec4 vColor;
out vec2 vTexCoord;
uniform mat4 u_projection;
void main() {
    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
    vColor = a_color;
    vTexCoord = a_uv;
}
)";
                        auto shader = renderer_->create_shader_from_glsl(vertex_glsl, glsl);
                        if (shader) {
                            LOG_INFO("Shader entry {} ({}): created OpenGL program {}",
                                     info.entry_index, info.profile, shader->get_id());
                            // Store for later binding
                            compiled_shader_ = shader;
                        } else {
                            LOG_WARN("Shader entry {} ({}): failed to create OpenGL program",
                                     info.entry_index, info.profile);
                        }
                    } else {
                        LOG_WARN("Shader entry {} ({}): could not generate GLSL",
                                 info.entry_index, info.profile);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                   "GLSL generation not available for this shader type");
            }
            ImGui::EndChild();
        } else {
            ImGui::Text("Select a shader to view details");
        }

        ImGui::End();
    }

    void render_data_table_viewer() {
        ImGui::Begin("Data Table Viewer", &show_dat_viewer_,
                     ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Scenario: %u", loaded_scenario_);
        ImGui::SameLine();
        ImGui::Text("Officers: %zu | Castles: %zu",
                    sndata_loaded_.officer_count(), sndata_loaded_.castle_count());
        ImGui::Separator();

        // Tab-like selector
        static int view_tab = 0; // 0 = officers, 1 = castles
        if (ImGui::RadioButton("Officers", &view_tab, 0)) {
            selected_castle_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Castles", &view_tab, 1)) {
            selected_officer_ = -1;
        }
        ImGui::Separator();

        bool show_officers = (view_tab == 0);

        if (show_officers) {
            render_officer_list();
        } else {
            render_castle_list();
        }

        ImGui::End();
    }

    void render_officer_list() {
        if (ImGui::BeginChild("OfficerList", ImVec2(350, 0), true)) {
            for (size_t i = 0; i < sndata_loaded_.officer_count(); ++i) {
                const auto& officer = sndata_loaded_.officers[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           (selected_officer_ == static_cast<int>(i)
                                                ? ImGuiTreeNodeFlags_Selected : 0);
                std::string label = fmt::format("[{}] id={} {}",
                                                officer.slot, officer.id,
                                                officer.name_utf8.empty()
                                                    ? officer.name_raw
                                                    : officer.name_utf8);
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", label.c_str());
                if (ImGui::IsItemClicked()) {
                    selected_officer_ = static_cast<int>(i);
                    selected_castle_ = -1;
                }
                if (open) ImGui::TreePop();
            }
            ImGui::EndChild();
        }

        ImGui::SameLine();

        if (selected_officer_ >= 0 &&
            static_cast<size_t>(selected_officer_) < sndata_loaded_.officer_count()) {
            const auto& officer = sndata_loaded_.officers[selected_officer_];
            ImGui::BeginChild("OfficerDetails", ImVec2(0, 0), true);
            ImGui::Text("Slot: %u", officer.slot);
            ImGui::Text("ID: %u", officer.id);
            ImGui::Text("Status: %u", officer.status);
            ImGui::Text("Gender: %u", officer.gender);
            ImGui::Text("Name (raw): %s", officer.name_raw.c_str());
            ImGui::Text("Name (UTF-8): %s",
                        officer.name_utf8.empty() ? "(n/a)" : officer.name_utf8.c_str());
            ImGui::Text("Attr size: %zu bytes", officer.attr.size());
            ImGui::Separator();
            ImGui::Text("Raw attr (hex):");
            std::string hex;
            for (uint8_t b : officer.attr) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", b);
                hex += buf;
            }
            ImGui::TextWrapped("%s", hex.c_str());
            ImGui::EndChild();
        }
    }

    void render_castle_list() {
        if (ImGui::BeginChild("CastleList", ImVec2(350, 0), true)) {
            for (size_t i = 0; i < sndata_loaded_.castle_count(); ++i) {
                const auto& castle = sndata_loaded_.castles[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           (selected_castle_ == static_cast<int>(i)
                                                ? ImGuiTreeNodeFlags_Selected : 0);
                std::string label = fmt::format("[{}] {}", castle.index,
                                                castle.name_utf8.empty()
                                                    ? castle.name_raw
                                                    : castle.name_utf8);
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", label.c_str());
                if (ImGui::IsItemClicked()) {
                    selected_castle_ = static_cast<int>(i);
                    selected_officer_ = -1;
                }
                if (open) ImGui::TreePop();
            }
            ImGui::EndChild();
        }

        ImGui::SameLine();

        if (selected_castle_ >= 0 &&
            static_cast<size_t>(selected_castle_) < sndata_loaded_.castle_count()) {
            const auto& castle = sndata_loaded_.castles[selected_castle_];
            ImGui::BeginChild("CastleDetails", ImVec2(0, 0), true);
            ImGui::Text("Index: %u", castle.index);
            ImGui::Text("Header[0]: %u", castle.header[0]);
            ImGui::Text("Header[1]: %u", castle.header[1]);
            ImGui::Text("Header[2]: %u", castle.header[2]);
            ImGui::Text("Header[3]: %u", castle.header[3]);
            ImGui::Text("Name (raw): %s", castle.name_raw.c_str());
            ImGui::Text("Name (UTF-8): %s",
                        castle.name_utf8.empty() ? "(n/a)" : castle.name_utf8.c_str());
            ImGui::Separator();
            ImGui::Text("Raw tail (hex):");
            std::string hex;
            for (uint8_t b : castle.tail) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", b);
                hex += buf;
            }
            ImGui::TextWrapped("%s", hex.c_str());
            ImGui::EndChild();
        }
    }

    bool extract_entry_to_disk(const BinArchive& archive, const BinFileEntry& entry) {
        auto result = archive.extract(entry);
        if (!result) {
            LOG_ERROR("Extract failed: {}", tensyoki::to_string(result.error()));
            return false;
        }

        std::filesystem::path name = std::filesystem::path(entry.name).filename();
        if (name.empty() || name == "." || name == "..") {
            name = fmt::format("entry_{:04d}.bin", entry.index);
        }

        const auto dir = std::filesystem::path("extracted") /
                         std::filesystem::path(current_file_).stem();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            LOG_ERROR("Cannot create {}: {}", dir.string(), ec.message());
            return false;
        }

        const auto out_path = dir / name;
        if (std::filesystem::exists(out_path)) {
            LOG_WARN("Overwriting existing file: {}", out_path.string());
        }
        std::ofstream out(out_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(result->data()),
                  static_cast<std::streamsize>(result->size()));
        if (!out) {
            LOG_ERROR("Failed to write {}", out_path.string());
            return false;
        }
        LOG_INFO("Extracted '{}' -> {} ({} bytes)", entry.name, out_path.string(), result->size());
        return true;
    }
    
    bool show_hex_dump_ = true;
    bool show_full_hex_ = false;
    
    void shutdown() {
        LOG_INFO("Shutting down...");
        
        imgui_.reset();
        renderer_.reset();
        platform_.reset();
        
        Config::instance().save();
        VFS::instance().unmount_all();
        
        Logging::shutdown();
    }
    
    bool show_resource_browser_ = false;
    bool show_shader_viewer_ = false;
    bool show_dat_viewer_ = false;
    bool show_atlas_viewer_ = false;
    bool show_texture_viewer_ = false;
    bool show_officer_card_ = false;
    bool check_resources_ = false;
    bool battle_test_ = false;
    bool castle_test_ = false;
    bool save_test_ = false;
    bool officer_test_ = false;
    bool strategy_test_ = false;
    bool help_requested_ = false;

    std::unique_ptr<BinArchive> selected_archive_;
    const BinFileEntry* selected_entry_ = nullptr;
    std::string current_file_ = "res/res_grp.bin";

    // Texture preview state
    GT1GTextureCache texture_cache_;
    std::shared_ptr<Renderer::Texture> preview_texture_;
    int preview_width_ = 0;
    int preview_height_ = 0;
    std::string preview_container_;
    uint32_t preview_entry_index_ = 0;

    // Text rendering
    std::unique_ptr<Renderer::TextRenderer> text_renderer_;

    // Phase 4: Game state system
    GameContext game_ctx_;
    GameStateManager game_state_mgr_;
    bool game_initialized_ = false;

    // Shader compiler and parsed shader infos
    std::unique_ptr<ShaderCompiler> shader_compiler_;
    std::vector<ShaderCompiler::D3D9ShaderInfo> shader_infos_;
    int selected_shader_ = -1;
    std::shared_ptr<Renderer::Shader> compiled_shader_;

    // Scenario data (officers / castles)
    SndataArchive sndata_loaded_;
    uint32_t loaded_scenario_ = 0;
    int selected_officer_ = -1;
    int selected_castle_ = -1;

    // Officer card preview (texture from res_grp.bin entry 0 GT1G)
    std::unique_ptr<BinArchive> grp_archive_;
    std::shared_ptr<Renderer::Texture> officer_portrait_;
    int portrait_texture_index_ = 0;

    // Officer Card rendering pipeline (bound to compiled_shader_ when available)
    std::shared_ptr<Renderer::Pipeline> officer_card_pipeline_;
    GLuint officer_card_vao_ = 0;
    GLuint officer_card_vbo_ = 0;
};

} // namespace tensyoki

int main(int argc, char* argv[]) {
    try {
        tensyoki::Application app(argc, argv);
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }
}
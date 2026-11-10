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
        
        // Set up the Chinese font
        imgui_->load_font_default(18.0f);
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
        return true;
    }
    
    void parse_args() {
        std::filesystem::path cli_path;

        for (int i = 1; i < argc_; ++i) {
            std::string_view arg = argv_[i];
            if (arg == "--check-resources") {
                check_resources_ = true;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: tensyoki [options] [data_root]\n"
                          << "  --check-resources  parse all res/*.bin containers and exit\n"
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
            
            // Render UI
            render_ui();
            
            // End the frame
            renderer_->end_frame();
            imgui_->render();
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

        if (selected_officer_ >= 0 &&
            static_cast<size_t>(selected_officer_) < sndata_loaded_.officer_count()) {
            const auto& officer = sndata_loaded_.officers[selected_officer_];

            // Display portrait texture (from res_grp.bin entry 0 GT1G)
            ImVec2 uv0(0, 1);
            ImVec2 uv1(1, 0);
            ImVec2 size(256.0f, 256.0f);
            ImGui::Image(static_cast<ImTextureID>(officer_portrait_->get_id()), size, uv0, uv1,
                         ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
            ImGui::SameLine();

            // Officer info
            ImGui::BeginGroup();
            ImGui::Text("Slot: %u", officer.slot);
            ImGui::Text("ID: %u", officer.id);
            ImGui::Text("Status: %u", officer.status);
            ImGui::Text("Gender: %u", officer.gender);
            ImGui::Separator();
            ImGui::Text("Name (UTF-8): %s",
                        officer.name_utf8.empty() ? officer.name_raw.c_str()
                                                  : officer.name_utf8.c_str());
            ImGui::EndGroup();
        } else {
            ImGui::Text("Select an officer from Data Table Viewer");
        }

        ImGui::End();
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
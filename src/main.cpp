/**
 * main.cpp - Entry point for the Tensyoki Linux port
 */

#include "core/config.hpp"
#include "core/filesystem.hpp"
#include "core/logging.hpp"
#include "core/math_utils.hpp"
#include "render/renderer.hpp"
#include "resource/bin_parser.hpp"
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
#include <fstream>
#include <system_error>

namespace tensyoki {

class Application {
public:
    Application(int argc, char* argv[]) : argc_(argc), argv_(argv) {}
    
    int run() {
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
        // Initialize logging
        Logging::init("tensyoki.log");
        LOG_INFO("=== Tensyoki Linux Port ===");
        LOG_INFO("Version: 0.1.0-dev");
        LOG_INFO("Build: {} {}", __DATE__, __TIME__);
        
        // Load configuration
        Config::instance().load("config.toml");
        
        // Parse command-line arguments for the data path
        parse_args();
        
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
        
        LOG_INFO("Initialization complete");
        return true;
    }
    
    void parse_args() {
        // Priority: command line > environment variables > config file > auto-detection
        if (argc_ > 1) {
            data_root_ = argv_[1];
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
                ImGui::MenuItem("Texture Atlas Viewer", nullptr, &show_atlas_viewer_);
                ImGui::MenuItem("Shader Viewer", nullptr, &show_shader_viewer_);
                ImGui::MenuItem("Data Table Viewer", nullptr, &show_dat_viewer_);
                ImGui::EndMenu();
            }
            
            ImGui::EndMainMenuBar();
        }
        
        // Texture atlas viewer
        if (show_atlas_viewer_ && atlas_viewer_) {
            atlas_viewer_->draw();
        }
        
        // Resource browser
        if (show_resource_browser_) {
            render_resource_browser();
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
        ImGui::Text("Hash: 0x%08X", entry.name_hash);
        ImGui::Text("Offset: 0x%08X (%u)", entry.offset, entry.offset);
        ImGui::Text("Size: 0x%08X (%u bytes)", entry.size, entry.size);
        ImGui::Text("Type: %u", entry.type);
        ImGui::Text("Flags: 0x%04X", entry.flags);
        ImGui::Text("Extension: %s", entry.guess_extension().c_str());
        
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
                        size_t idx = offset + col;
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
                        size_t idx = offset + col;
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
                                   tensyoki::to_string(view_result.error()));
            }
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
            name = fmt::format("entry_{:08X}.bin", entry.name_hash);
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

    std::unique_ptr<BinArchive> selected_archive_;
    const BinFileEntry* selected_entry_ = nullptr;
    std::string current_file_ = "res/res_grp.bin";
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
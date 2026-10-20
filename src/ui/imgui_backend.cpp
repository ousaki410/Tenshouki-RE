/**
 * imgui_backend.cpp - Dear ImGui backend implementation
 */

#include "imgui_backend.hpp"
#include "logging.hpp"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL3/SDL.h>
#include <vector>

namespace tensyoki {

struct ImGuiBackend::Impl {
    ImGuiContext* ctx = nullptr;
    bool initialized = false;
    bool use_vulkan = false;
    SDL_Window* window = nullptr;
    std::string ini_filename;  // Store the ini filename to keep it alive
};

ImGuiBackend::ImGuiBackend() : impl_(std::make_unique<Impl>()) {}
ImGuiBackend::~ImGuiBackend() { shutdown(); }

bool ImGuiBackend::init(const InitParams& params) {
    if (impl_->initialized) {
        LOG_WARN("ImGuiBackend already initialized");
        return true;
    }
    
    impl_->window = static_cast<SDL_Window*>(params.window_handle);
    if (!impl_->window) {
        LOG_ERROR("Invalid window handle");
        return false;
    }
    
    // Save the ini filename so it outlives the entire ImGui context
    impl_->ini_filename = params.ini_filename ? params.ini_filename : "imgui.ini";
    
    // Create the ImGui context
    impl_->ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(impl_->ctx);
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    if (params.enable_docking) {
        // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // Not available in all versions
    }
    
    // Viewports disabled for compatibility
    // if (params.enable_viewports) {
    //     io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    // }
    
    io.IniFilename = impl_->ini_filename.c_str();
    io.FontGlobalScale = params.font_scale;
    
    // Initialize the SDL3 backend
    if (!ImGui_ImplSDL3_InitForOpenGL(impl_->window, 
                                        SDL_GL_GetCurrentContext())) {
        LOG_ERROR("ImGui_ImplSDL3_InitForOpenGL failed");
        return false;
    }
    
    // Initialize the OpenGL3 backend
    const char* glsl_version = "#version 450";
    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        LOG_ERROR("ImGui_ImplOpenGL3_Init failed");
        return false;
    }
    
    // Set the default style
    set_style_dark();
    
    // Load the default font
    load_font_default(18.0f * params.font_scale);
    
    impl_->initialized = true;
    LOG_INFO("ImGuiBackend initialized");
    return true;
}

void ImGuiBackend::shutdown() {
    if (!impl_->initialized) return;
    
    ImGui::SetCurrentContext(impl_->ctx);
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(impl_->ctx);
    
    impl_->ctx = nullptr;
    impl_->initialized = false;
    LOG_INFO("ImGuiBackend shutdown");
}

void ImGuiBackend::new_frame(float /*delta_time*/) {
    if (!impl_->initialized) return;
    
    ImGui::SetCurrentContext(impl_->ctx);
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::render() {
    if (!impl_->initialized) return;
    
    ImGui::SetCurrentContext(impl_->ctx);
    
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    // Viewport rendering disabled for compatibility
}

void ImGuiBackend::process_event(const SDL_Event* event) {
    if (!impl_->initialized) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

ImFont* ImGuiBackend::load_font(const std::string& path, float size, 
                                const ImWchar* ranges) {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size, nullptr, ranges);
    if (!font) {
        LOG_ERROR("Failed to load font: {}", path);
    }
    return font;
}

ImFont* ImGuiBackend::load_font_default(float size) {
    (void)size;
    ImGui::SetCurrentContext(impl_->ctx);
    ImGuiIO& io = ImGui::GetIO();
    // Use the default font
    ImFont* font = io.Fonts->AddFontDefault();
    if (font) {
        font->Scale = 1.0f;
    }
    return font;
}

void ImGuiBackend::set_default_font(ImFont* font) {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGui::GetIO().FontDefault = font;
}

void ImGuiBackend::set_style_dark() {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGui::StyleColorsDark();
}

void ImGuiBackend::set_style_light() {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGui::StyleColorsLight();
}

void ImGuiBackend::set_style_classic() {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGui::StyleColorsClassic();
}

void ImGuiBackend::scale_style(float factor) {
    ImGui::SetCurrentContext(impl_->ctx);
    ImGui::GetStyle().ScaleAllSizes(factor);
}

void ImGuiBackend::on_resize(int /*width*/, int /*height*/) {
    // ImGui handles resize automatically through SDL events
}

// ImGui window helpers
namespace imgui_utils {

// Help marker (tooltip on hover)
void help_marker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Read-only input field
bool input_text_readonly(const char* label, const char* value) {
    ImGui::InputText(label, const_cast<char*>(value), strlen(value), ImGuiInputTextFlags_ReadOnly);
    return false;
}

// Color editor (with alpha)
bool color_edit4(const char* label, float col[4], ImGuiColorEditFlags flags) {
    return ImGui::ColorEdit4(label, col, flags);
}

// Vector editor
bool drag_float2(const char* label, float v[2], float speed, 
                 float min, float max, const char* format) {
    return ImGui::DragFloat2(label, v, speed, min, max, format);
}

bool drag_float3(const char* label, float v[3], float speed,
                 float min, float max, const char* format) {
    return ImGui::DragFloat3(label, v, speed, min, max, format);
}

bool drag_float4(const char* label, float v[4], float speed,
                 float min, float max, const char* format) {
    return ImGui::DragFloat4(label, v, speed, min, max, format);
}

// Slider with label
bool slider_float(const char* label, float* v, float min, float max, 
                  const char* format, ImGuiSliderFlags flags) {
    return ImGui::SliderFloat(label, v, min, max, format, flags);
}

bool slider_int(const char* label, int* v, int min, int max, 
                const char* format, ImGuiSliderFlags flags) {
    return ImGui::SliderInt(label, v, min, max, format, flags);
}

// Combo box (array of strings)
bool combo(const char* label, int* current, const char* const* items, int count) {
    return ImGui::Combo(label, current, items, count);
}

// Table helpers
void table_setup_column(const char* label, ImGuiTableColumnFlags flags, 
                        float init_width) {
    ImGui::TableSetupColumn(label, flags, init_width);
}

bool table_next_row(ImGuiTableRowFlags flags) {
    ImGui::TableNextRow(flags);
    return true;
}

void table_next_column() {
    ImGui::TableNextColumn();
}

void table_set_column_index(int column) {
    ImGui::TableSetColumnIndex(column);
}

// File dropdown
void file_drop_target(const char* label, std::function<void(const std::string&)> callback) {
    (void)label;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
            const char* path = static_cast<const char*>(payload->Data);
            callback(path);
        }
        ImGui::EndDragDropTarget();
    }
}

// Progress bar (with text)
void progress_bar(float fraction, const ImVec2& size, 
                  const char* overlay) {
    ImGui::ProgressBar(fraction, size, overlay);
}

// Color button (with picker)
bool color_button_with_picker(const char* label, float col[4], 
                              ImGuiColorEditFlags flags) {
    bool changed = ImGui::ColorButton(label, ImVec4(col[0], col[1], col[2], col[3]), flags);
    if (changed) {
        ImGui::OpenPopup("color_picker");
    }
    if (ImGui::BeginPopup("color_picker")) {
        ImGui::ColorPicker4("picker", col, flags);
        ImGui::EndPopup();
    }
    return changed;
}

// Tab bar (vertical)
bool begin_vertical_tab_bar(const char* str_id) {
    return ImGui::BeginTabBar(str_id, ImGuiTabBarFlags_NoTabListScrollingButtons | 
                              ImGuiTabBarFlags_FittingPolicyScroll);
}

void end_vertical_tab_bar() {
    ImGui::EndTabBar();
}

bool vertical_tab_item(const char* label, bool* selected) {
    bool open = true;
    bool result = ImGui::BeginTabItem(label, selected ? &open : nullptr, 
                                       selected ? ImGuiTabItemFlags_None : ImGuiTabItemFlags_NoCloseWithMiddleMouseButton);
    if (result && selected) *selected = open;
    if (result) ImGui::EndTabItem();
    return result;
}

// Search box
bool search_box(const char* label, char* buf, size_t buf_size) {
    return ImGui::InputTextWithHint(label, "Search...", buf, buf_size);
}

} // namespace imgui_utils

} // namespace tensyoki
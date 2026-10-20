/**
 * imgui_backend.hpp - Dear ImGui SDL3 + OpenGL/Vulkan backend wrapper
 */

#pragma once

#include <memory>
#include <string>
#include <functional>
#include <imgui.h>
#include <SDL3/SDL.h>

namespace tensyoki {

class Renderer;  // Forward declaration

class ImGuiBackend {
public:
    struct InitParams {
        void* window_handle = nullptr;  // SDL_Window*
        void* device = nullptr;         // Backend device pointer
        bool enable_docking = true;
        bool enable_viewports = true;
        const char* ini_filename = "imgui.ini";  // Use const char* to avoid lifetime issues
        float font_scale = 1.0f;
    };
    
    ImGuiBackend();
    ~ImGuiBackend();
    
    bool init(const InitParams& params);
    void shutdown();
    
    void new_frame(float delta_time);
    void render();
    
    void process_event(const SDL_Event* event);
    
    // Font management
    ImFont* load_font(const std::string& path, float size, 
                      const ImWchar* ranges = nullptr);
    ImFont* load_font_default(float size);
    void set_default_font(ImFont* font);
    
    // Chinese font ranges
    static const ImWchar* get_chinese_ranges();
    static const ImWchar* get_japanese_ranges();
    static const ImWchar* get_korean_ranges();
    
    // Style
    void set_style_dark();
    void set_style_light();
    void set_style_classic();
    void scale_style(float factor);
    
    // Access the ImGui context
    ImGuiContext* get_context() const { return ctx_; }
    
    // Window size callback
    void on_resize(int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ImGuiContext* ctx_ = nullptr;
};

// ImGui window helpers
namespace imgui_utils {

// Centered popup
void open_centered_popup(const char* name, ImVec2 size = ImVec2(0, 0));

// Help marker (tooltip on hover)
void help_marker(const char* desc);

// Read-only input field
bool input_text_readonly(const char* label, const char* value);

// Color editor (with alpha)
bool color_edit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);

// Vector editor
bool drag_float2(const char* label, float v[2], float speed = 0.1f, 
                 float min = 0, float max = 0, const char* format = "%.3f");
bool drag_float3(const char* label, float v[3], float speed = 0.1f,
                 float min = 0, float max = 0, const char* format = "%.3f");
bool drag_float4(const char* label, float v[4], float speed = 0.1f,
                 float min = 0, float max = 0, const char* format = "%.3f");

// Slider with label
bool slider_float(const char* label, float* v, float min, float max, 
                  const char* format = "%.3f", ImGuiSliderFlags flags = 0);
bool slider_int(const char* label, int* v, int min, int max, 
                const char* format = "%d", ImGuiSliderFlags flags = 0);

// Combo box (array of strings)
bool combo(const char* label, int* current, const char* const* items, int count);

// Table helpers
void table_setup_column(const char* label, ImGuiTableColumnFlags flags = 0, 
                        float init_width = 0.0f);
bool table_next_row(ImGuiTableRowFlags flags = 0);
void table_next_column();
void table_set_column_index(int column);

// File dropdown
void file_drop_target(const char* label, std::function<void(const std::string&)> callback);

// Progress bar (with text)
void progress_bar(float fraction, const ImVec2& size = ImVec2(-1, 0), 
                  const char* overlay = nullptr);

// Color button (with picker)
bool color_button_with_picker(const char* label, float col[4], 
                              ImGuiColorEditFlags flags = 0);

// Tab bar (vertical)
bool begin_vertical_tab_bar(const char* str_id);
void end_vertical_tab_bar();
bool vertical_tab_item(const char* label, bool* selected = nullptr);

// Search box
bool search_box(const char* label, char* buf, size_t buf_size);

// Separator text
void separator_text(const char* label);

// Spacing
void spacing(int count = 1);
void indent(float width = 0);
void unindent(float width = 0);

} // namespace imgui_utils

} // namespace tensyoki
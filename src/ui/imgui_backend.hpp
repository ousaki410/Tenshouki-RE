/**
 * imgui_backend.hpp - Dear ImGui SDL3 + OpenGL/Vulkan 后端封装
 */

#pragma once

#include <memory>
#include <string>
#include <functional>
#include <imgui.h>
#include <SDL3/SDL.h>

namespace tensyoki {

class Renderer;  // 前向声明

class ImGuiBackend {
public:
    struct InitParams {
        void* window_handle = nullptr;  // SDL_Window*
        void* device = nullptr;         // 后端设备指针
        bool enable_docking = true;
        bool enable_viewports = true;
        const char* ini_filename = "imgui.ini";  // 用 const char* 避免生命周期问题
        float font_scale = 1.0f;
    };
    
    ImGuiBackend();
    ~ImGuiBackend();
    
    bool init(const InitParams& params);
    void shutdown();
    
    void new_frame(float delta_time);
    void render();
    
    void process_event(const SDL_Event* event);
    
    // 字体管理
    ImFont* load_font(const std::string& path, float size, 
                      const ImWchar* ranges = nullptr);
    ImFont* load_font_default(float size);
    void set_default_font(ImFont* font);
    
    // 中文字体范围
    static const ImWchar* get_chinese_ranges();
    static const ImWchar* get_japanese_ranges();
    static const ImWchar* get_korean_ranges();
    
    // 样式
    void set_style_dark();
    void set_style_light();
    void set_style_classic();
    void scale_style(float factor);
    
    // 访问 ImGui 上下文
    ImGuiContext* get_context() const { return ctx_; }
    
    // 窗口大小回调
    void on_resize(int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ImGuiContext* ctx_ = nullptr;
};

// ImGui 窗口辅助函数
namespace imgui_utils {

// 居中弹窗
void open_centered_popup(const char* name, ImVec2 size = ImVec2(0, 0));

// 帮助标记 (悬停显示提示)
void help_marker(const char* desc);

// 只读输入框
bool input_text_readonly(const char* label, const char* value);

// 颜色编辑器 (带 Alpha)
bool color_edit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);

// 向量编辑器
bool drag_float2(const char* label, float v[2], float speed = 0.1f, 
                 float min = 0, float max = 0, const char* format = "%.3f");
bool drag_float3(const char* label, float v[3], float speed = 0.1f,
                 float min = 0, float max = 0, const char* format = "%.3f");
bool drag_float4(const char* label, float v[4], float speed = 0.1f,
                 float min = 0, float max = 0, const char* format = "%.3f");

// 带标签的滑块
bool slider_float(const char* label, float* v, float min, float max, 
                  const char* format = "%.3f", ImGuiSliderFlags flags = 0);
bool slider_int(const char* label, int* v, int min, int max, 
                const char* format = "%d", ImGuiSliderFlags flags = 0);

// 组合框 (字符串数组)
bool combo(const char* label, int* current, const char* const* items, int count);

// 表格辅助
void table_setup_column(const char* label, ImGuiTableColumnFlags flags = 0, 
                        float init_width = 0.0f);
bool table_next_row(ImGuiTableRowFlags flags = 0);
void table_next_column();
void table_set_column_index(int column);

// 文件下拉
void file_drop_target(const char* label, std::function<void(const std::string&)> callback);

// 进度条 (带文本)
void progress_bar(float fraction, const ImVec2& size = ImVec2(-1, 0), 
                  const char* overlay = nullptr);

// 颜色按钮 (带拾取器)
bool color_button_with_picker(const char* label, float col[4], 
                              ImGuiColorEditFlags flags = 0);

// 标签页栏 (垂直)
bool begin_vertical_tab_bar(const char* str_id);
void end_vertical_tab_bar();
bool vertical_tab_item(const char* label, bool* selected = nullptr);

// 搜索框
bool search_box(const char* label, char* buf, size_t buf_size);

// 分隔线文本
void separator_text(const char* label);

// 间距
void spacing(int count = 1);
void indent(float width = 0);
void unindent(float width = 0);

} // namespace imgui_utils

} // namespace tensyoki
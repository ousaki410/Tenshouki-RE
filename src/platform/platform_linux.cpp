/**
 * platform_linux.cpp - Linux platform implementation (SDL3)
 */

#include "platform.hpp"
#include "logging.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <memory>
#include <vector>
#include <algorithm>

namespace tensyoki {

class LinuxPlatform : public Platform {
public:
    LinuxPlatform() = default;
    ~LinuxPlatform() override { shutdown(); }
    
    bool init(const WindowParams& params) override {
        // Initialize SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
            LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            return false;
        }
        
        // Set OpenGL attributes
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        
        if (params.msaa_samples > 0) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, params.msaa_samples);
        }
        
        // Enable sRGB
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
        
        // Window flags
        Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
        if (params.resizable) window_flags |= SDL_WINDOW_RESIZABLE;
        if (params.high_dpi) window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (params.borderless) window_flags |= SDL_WINDOW_BORDERLESS;
        
        // Create window
        window_ = SDL_CreateWindow(
            params.title.c_str(),
            params.width, params.height,
            window_flags
        );
        
        if (!window_) {
            LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
            return false;
        }
        
        // Create OpenGL context
        gl_context_ = SDL_GL_CreateContext(window_);
        if (!gl_context_) {
            LOG_ERROR("SDL_GL_CreateContext failed: {}", SDL_GetError());
            return false;
        }
        
        // Set VSync
        SDL_GL_SetSwapInterval(params.vsync ? 1 : 0);
        
        // Show window
        SDL_ShowWindow(window_);
        
        // Set icon
        if (!params.icon_path.empty()) {
            set_icon(params.icon_path);
        }
        
        // Set DPI awareness
        SDL_SetAppMetadata("Tensyoki Linux Port", "0.1.0", "org.tensyoki.port");
        
        LOG_INFO("Linux platform initialized: {}x{}", params.width, params.height);
        return true;
    }
    
    void shutdown() override {
        if (gl_context_) {
            SDL_GL_DestroyContext(gl_context_);
            gl_context_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }
    
    void poll_events() override {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }
    }
    
    void wait_events() override {
        SDL_Event event;
        if (SDL_WaitEvent(&event)) {
            handle_event(event);
        }
    }
    
    void set_title(const std::string& title) override {
        if (window_) SDL_SetWindowTitle(window_, title.c_str());
    }
    
    void set_size(int width, int height) override {
        if (window_) SDL_SetWindowSize(window_, width, height);
    }
    
    void get_size(int& width, int& height) const override {
        if (window_) SDL_GetWindowSize(window_, &width, &height);
    }
    
    void set_fullscreen(bool fullscreen) override {
        if (window_) {
            if (fullscreen) {
                int mode_count = 0;
                SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window_), &mode_count);
                if (modes && mode_count > 0) {
                    SDL_SetWindowFullscreenMode(window_, modes[0]);
                }
                if (modes) SDL_free(modes);
            } else {
                SDL_SetWindowFullscreenMode(window_, nullptr);
            }
        }
    }
    
    bool is_fullscreen() const override {
        if (window_) {
            return SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN;
        }
        return false;
    }
    
    void toggle_fullscreen() override {
        set_fullscreen(!is_fullscreen());
    }
    
    void set_vsync(bool enabled) override {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
    
    void set_icon(const std::filesystem::path& icon_path) override {
        if (!window_) return;
        
        SDL_Surface* icon = SDL_LoadBMP(icon_path.c_str());
        if (icon) {
            SDL_SetWindowIcon(window_, icon);
            SDL_DestroySurface(icon);
        } else {
            LOG_WARN("Failed to load window icon: {}", icon_path.string());
        }
    }
    
    void minimize() override { if (window_) SDL_MinimizeWindow(window_); }
    void maximize() override { if (window_) SDL_MaximizeWindow(window_); }
    void restore() override { if (window_) SDL_RestoreWindow(window_); }
    void show() override { if (window_) SDL_ShowWindow(window_); }
    void hide() override { if (window_) SDL_HideWindow(window_); }
    
    void set_position(int x, int y) override {
        if (window_) SDL_SetWindowPosition(window_, x, y);
    }
    
    void get_position(int& x, int& y) const override {
        if (window_) SDL_GetWindowPosition(window_, &x, &y);
    }
    
    std::vector<DisplayMode> get_display_modes() const override {
        std::vector<DisplayMode> modes;
        int display_count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
        
        for (int i = 0; i < display_count; ++i) {
            int mode_count = 0;
            SDL_DisplayMode** sdl_modes = SDL_GetFullscreenDisplayModes(displays[i], &mode_count);
            
            for (int j = 0; j < mode_count; ++j) {
                DisplayMode mode;
                mode.width = sdl_modes[j]->w;
                mode.height = sdl_modes[j]->h;
                mode.refresh_rate = static_cast<int>(sdl_modes[j]->refresh_rate);
                modes.push_back(mode);
            }
        }
        
        if (displays) SDL_free(displays);
        return modes;
    }
    
    DisplayMode get_current_display_mode() const override {
        DisplayMode mode = {0, 0, 0, true};
        if (window_) {
            SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
            const SDL_DisplayMode* sdl_mode = SDL_GetCurrentDisplayMode(display);
            if (sdl_mode) {
                mode.width = sdl_mode->w;
                mode.height = sdl_mode->h;
                mode.refresh_rate = static_cast<int>(sdl_mode->refresh_rate);
            }
        }
        return mode;
    }
    
    void set_clipboard_text(const std::string& text) override {
        SDL_SetClipboardText(text.c_str());
    }
    
    std::string get_clipboard_text() override {
        char* text = SDL_GetClipboardText();
        std::string result = text ? text : "";
        SDL_free(text);
        return result;
    }
    
    std::optional<std::filesystem::path> open_file_dialog(
        const std::string& title,
        const std::filesystem::path& default_path,
        const std::vector<std::pair<std::string, std::string>>& filters
    ) override {
        (void)title; (void)default_path; (void)filters;
        // TODO: use zenity/kdialog or the native SDL3 dialog (if available)
        LOG_WARN("File dialog not implemented on Linux");
        return std::nullopt;
    }
    
    std::optional<std::filesystem::path> save_file_dialog(
        const std::string& title,
        const std::filesystem::path& default_path,
        const std::vector<std::pair<std::string, std::string>>& filters
    ) override {
        (void)title; (void)default_path; (void)filters;
        LOG_WARN("Save file dialog not implemented on Linux");
        return std::nullopt;
    }
    
    std::optional<std::filesystem::path> select_folder_dialog(
        const std::string& title,
        const std::filesystem::path& default_path
    ) override {
        (void)title; (void)default_path;
        LOG_WARN("Folder dialog not implemented on Linux");
        return std::nullopt;
    }
    
    bool is_key_pressed(int keycode) const override {
        const bool* state = SDL_GetKeyboardState(nullptr);
        SDL_Scancode scancode = static_cast<SDL_Scancode>(keycode);
        return scancode < SDL_SCANCODE_COUNT && state[scancode];
    }
    
    bool is_mouse_button_pressed(int button) const override {
        Uint32 state = SDL_GetMouseState(nullptr, nullptr);
        return state & SDL_BUTTON_MASK(button + 1);
    }
    
    void get_mouse_position(int& x, int& y) const override {
        float fx, fy;
        SDL_GetMouseState(&fx, &fy);
        x = static_cast<int>(fx);
        y = static_cast<int>(fy);
    }
    
    void set_mouse_position(int x, int y) override {
        if (window_) SDL_WarpMouseInWindow(window_, x, y);
    }
    
    void set_mouse_captured(bool captured) override {
        if (window_) SDL_SetWindowMouseGrab(window_, captured);
        SDL_SetWindowRelativeMouseMode(window_, captured);
    }
    
    bool is_mouse_captured() const override {
        if (window_) return SDL_GetWindowMouseGrab(window_);
        return false;
    }
    
    void set_cursor_visible(bool visible) override {
        if (visible) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
    
    void set_cursor_shape(int shape) override {
        static SDL_SystemCursor cursors[] = {
            SDL_SYSTEM_CURSOR_DEFAULT,
            SDL_SYSTEM_CURSOR_TEXT,
            SDL_SYSTEM_CURSOR_CROSSHAIR,
            SDL_SYSTEM_CURSOR_POINTER,
            SDL_SYSTEM_CURSOR_EW_RESIZE,
            SDL_SYSTEM_CURSOR_NS_RESIZE,
            SDL_SYSTEM_CURSOR_NOT_ALLOWED
        };
        if (shape >= 0 && shape < static_cast<int>(std::size(cursors))) {
            // Cache cursors to avoid leaks
            static SDL_Cursor* cached_cursors[7] = {nullptr};
            if (!cached_cursors[shape]) {
                cached_cursors[shape] = SDL_CreateSystemCursor(cursors[shape]);
            }
            if (cached_cursors[shape]) {
                SDL_SetCursor(cached_cursors[shape]);
            }
        }
    }
    
    int get_joystick_count() const override {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
        if (joysticks) SDL_free(joysticks);
        return count;
    }
    
    std::string get_joystick_name(int index) const override {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
        if (index < 0 || index >= count) return "";
        SDL_Joystick* joy = SDL_OpenJoystick(joysticks[index]);
        if (joysticks) SDL_free(joysticks);
        if (!joy) return "";
        const char* name = SDL_GetJoystickName(joy);
        std::string result = name ? name : "";
        SDL_CloseJoystick(joy);
        return result;
    }
    
    bool is_joystick_button_pressed(int joystick, int button) const override {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
        if (joystick < 0 || joystick >= count) {
            if (joysticks) SDL_free(joysticks);
            return false;
        }
        SDL_Joystick* joy = SDL_OpenJoystick(joysticks[joystick]);
        if (joysticks) SDL_free(joysticks);
        if (!joy) return false;
        bool pressed = SDL_GetJoystickButton(joy, button);
        SDL_CloseJoystick(joy);
        return pressed;
    }
    
    float get_joystick_axis(int joystick, int axis) const override {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
        if (joystick < 0 || joystick >= count) {
            if (joysticks) SDL_free(joysticks);
            return 0.0f;
        }
        SDL_Joystick* joy = SDL_OpenJoystick(joysticks[joystick]);
        if (joysticks) SDL_free(joysticks);
        if (!joy) return 0.0f;
        Sint16 value = SDL_GetJoystickAxis(joy, axis);
        SDL_CloseJoystick(joy);
        return static_cast<float>(value) / 32767.0f;
    }
    
    bool is_joystick_connected(int index) const override {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
        bool result = (index >= 0 && index < count);
        if (joysticks) SDL_free(joysticks);
        return result;
    }
    
    void request_quit() override { should_quit_ = true; }
    bool should_quit() const override { return should_quit_; }
    
    double get_time() const override {
        return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
    }
    
    void sleep(uint32_t ms) override {
        SDL_Delay(ms);
    }
    
    void* get_window_handle() const override {
        return window_;
    }
    
    void* get_display_handle() const override {
        // Wayland: wl_display*, X11: Display*
        // SDL3 doesn't provide direct WM info access, return nullptr for now
        return nullptr;
    }
    
    void swap_buffers() override {
        if (window_) SDL_GL_SwapWindow(window_);
    }
    
    float get_dpi_scale() const override {
        if (window_) {
            return SDL_GetWindowDisplayScale(window_);
        }
        return 1.0f;
    }
    
    void set_dpi_aware(bool /*aware*/) override {
        // Handled automatically by SDL3
    }
    
    void set_resize_callback(ResizeCallback cb) override { resize_cb_ = std::move(cb); }
    void set_close_callback(CloseCallback cb) override { close_cb_ = std::move(cb); }
    void set_key_callback(KeyCallback cb) override { key_cb_ = std::move(cb); }
    void set_char_callback(CharCallback cb) override { char_cb_ = std::move(cb); }
    void set_mouse_button_callback(MouseButtonCallback cb) override { mouse_btn_cb_ = std::move(cb); }
    void set_mouse_move_callback(MouseMoveCallback cb) override { mouse_move_cb_ = std::move(cb); }
    void set_scroll_callback(ScrollCallback cb) override { scroll_cb_ = std::move(cb); }
    void set_focus_callback(FocusCallback cb) override { focus_cb_ = std::move(cb); }
    void set_drop_callback(DropCallback cb) override { drop_cb_ = std::move(cb); }

private:
    void handle_event(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                should_quit_ = true;
                if (close_cb_ && !close_cb_()) should_quit_ = false;
                break;
                
            case SDL_EVENT_WINDOW_RESIZED:
                if (resize_cb_) resize_cb_(event.window.data1, event.window.data2);
                break;
                
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (close_cb_ && !close_cb_()) {
                    // Block close
                } else {
                    should_quit_ = true;
                }
                break;
                
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (key_cb_) key_cb_(event.key.key, event.key.scancode, 
                                     event.type == SDL_EVENT_KEY_DOWN ? 1 : 0, event.key.mod);
                break;
                
            case SDL_EVENT_TEXT_INPUT:
                if (char_cb_) char_cb_(event.text.text[0]);  // simplified
                break;
                
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (mouse_btn_cb_) mouse_btn_cb_(event.button.button, 
                                                 event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0, 0);
                break;
                
            case SDL_EVENT_MOUSE_MOTION:
                if (mouse_move_cb_) mouse_move_cb_(static_cast<int>(event.motion.x), 
                                                   static_cast<int>(event.motion.y));
                break;
                
            case SDL_EVENT_MOUSE_WHEEL:
                if (scroll_cb_) scroll_cb_(event.wheel.x, event.wheel.y);
                break;
                
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (focus_cb_) focus_cb_(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                break;
                
            case SDL_EVENT_DROP_FILE:
                if (drop_cb_) {
                    std::vector<std::filesystem::path> paths;
                    paths.emplace_back(event.drop.data);
                    drop_cb_(paths);
                }
                break;
                
            case SDL_EVENT_DROP_COMPLETE:
                // Multi-file drop could be handled here
                break;
        }
    }
    
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    bool should_quit_ = false;
    
    ResizeCallback resize_cb_;
    CloseCallback close_cb_;
    KeyCallback key_cb_;
    CharCallback char_cb_;
    MouseButtonCallback mouse_btn_cb_;
    MouseMoveCallback mouse_move_cb_;
    ScrollCallback scroll_cb_;
    FocusCallback focus_cb_;
    DropCallback drop_cb_;
};

std::unique_ptr<Platform> Platform::create() {
    return std::make_unique<LinuxPlatform>();
}

} // namespace tensyoki
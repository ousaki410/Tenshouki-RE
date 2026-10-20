/**
 * platform.hpp - platform abstraction layer
 * Common interface for Linux/Windows/macOS
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <filesystem>
#include <optional>

namespace tensyoki {

class Platform {
public:
    struct WindowParams {
        std::string title = "Tensyoki";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool resizable = true;
        bool vsync = true;
        bool high_dpi = true;
        bool borderless = false;
        int msaa_samples = 0;
        std::string icon_path;  // window icon
    };
    
    struct DisplayMode {
        int width = 0;
        int height = 0;
        int refresh_rate = 0;
        bool is_current = false;
    };
    
    virtual ~Platform() = default;
    
    // Create platform instance
    static std::unique_ptr<Platform> create();
    
    // Initialize
    virtual bool init(const WindowParams& params) = 0;
    
    // Shutdown
    virtual void shutdown() = 0;
    
    // Main loop event handling
    virtual void poll_events() = 0;
    virtual void wait_events() = 0;
    
    // Window management
    virtual void set_title(const std::string& title) = 0;
    virtual void set_size(int width, int height) = 0;
    virtual void get_size(int& width, int& height) const = 0;
    virtual void set_fullscreen(bool fullscreen) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void toggle_fullscreen() = 0;
    virtual void set_vsync(bool enabled) = 0;
    virtual void set_icon(const std::filesystem::path& icon_path) = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void set_position(int x, int y) = 0;
    virtual void get_position(int& x, int& y) const = 0;
    
    // Display modes
    virtual std::vector<DisplayMode> get_display_modes() const = 0;
    virtual DisplayMode get_current_display_mode() const = 0;
    
    // Clipboard
    virtual void set_clipboard_text(const std::string& text) = 0;
    virtual std::string get_clipboard_text() = 0;
    
    // File dialogs
    virtual std::optional<std::filesystem::path> open_file_dialog(
        const std::string& title,
        const std::filesystem::path& default_path = "",
        const std::vector<std::pair<std::string, std::string>>& filters = {}
    ) = 0;
    
    virtual std::optional<std::filesystem::path> save_file_dialog(
        const std::string& title,
        const std::filesystem::path& default_path = "",
        const std::vector<std::pair<std::string, std::string>>& filters = {}
    ) = 0;
    
    virtual std::optional<std::filesystem::path> select_folder_dialog(
        const std::string& title,
        const std::filesystem::path& default_path = ""
    ) = 0;
    
    // Input
    virtual bool is_key_pressed(int keycode) const = 0;
    virtual bool is_mouse_button_pressed(int button) const = 0;
    virtual void get_mouse_position(int& x, int& y) const = 0;
    virtual void set_mouse_position(int x, int y) = 0;
    virtual void set_mouse_captured(bool captured) = 0;
    virtual bool is_mouse_captured() const = 0;
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void set_cursor_shape(int shape) = 0;  // standard cursor shape
    
    // Gamepad
    virtual int get_joystick_count() const = 0;
    virtual std::string get_joystick_name(int index) const = 0;
    virtual bool is_joystick_button_pressed(int joystick, int button) const = 0;
    virtual float get_joystick_axis(int joystick, int axis) const = 0;
    virtual bool is_joystick_connected(int index) const = 0;
    
    // System
    virtual void request_quit() = 0;
    virtual bool should_quit() const = 0;
    virtual double get_time() const = 0;  // seconds, high precision
    virtual void sleep(uint32_t ms) = 0;
    
    // Native handles (for renderer)
    virtual void* get_window_handle() const = 0;
    virtual void* get_display_handle() const = 0;
    
    // Swap buffers
    virtual void swap_buffers() = 0;
    
    // DPI scaling
    virtual float get_dpi_scale() const = 0;
    virtual void set_dpi_aware(bool aware) = 0;
    
    // Callbacks
    using ResizeCallback = std::function<void(int width, int height)>;
    using CloseCallback = std::function<bool()>;  // return true to allow closing
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using CharCallback = std::function<void(uint32_t codepoint)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using MouseMoveCallback = std::function<void(int x, int y)>;
    using ScrollCallback = std::function<void(float x, float y)>;
    using FocusCallback = std::function<void(bool focused)>;
    using DropCallback = std::function<void(const std::vector<std::filesystem::path>& paths)>;
    
    virtual void set_resize_callback(ResizeCallback cb) = 0;
    virtual void set_close_callback(CloseCallback cb) = 0;
    virtual void set_key_callback(KeyCallback cb) = 0;
    virtual void set_char_callback(CharCallback cb) = 0;
    virtual void set_mouse_button_callback(MouseButtonCallback cb) = 0;
    virtual void set_mouse_move_callback(MouseMoveCallback cb) = 0;
    virtual void set_scroll_callback(ScrollCallback cb) = 0;
    virtual void set_focus_callback(FocusCallback cb) = 0;
    virtual void set_drop_callback(DropCallback cb) = 0;
};

// Key code definitions (SDL compatible)
namespace Key {
    enum : int {
        Unknown = -1,
        Space = 32,
        Apostrophe = 39,
        Comma = 44,
        Minus = 45,
        Period = 46,
        Slash = 47,
        D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        Semicolon = 59,
        Equal = 61,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftBracket = 91,
        Backslash = 92,
        RightBracket = 93,
        GraveAccent = 96,
        World1 = 161,
        World2 = 162,
        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,
        Insert = 260,
        Delete = 261,
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,
        PageUp = 266,
        PageDown = 267,
        Home = 268,
        End = 269,
        CapsLock = 280,
        ScrollLock = 281,
        NumLock = 282,
        PrintScreen = 283,
        Pause = 284,
        F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        F25 = 313,
        KP0 = 320, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDecimal = 330,
        KPDivide = 331,
        KPMultiply = 332,
        KPSubtract = 333,
        KPAdd = 334,
        KPEnter = 335,
        KPEqual = 336,
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        LeftSuper = 343,
        RightShift = 344,
        RightControl = 345,
        RightAlt = 346,
        RightSuper = 347,
        Menu = 348,
    };
}

// Mouse buttons
namespace MouseButton {
    enum : int {
        Left = 0,
        Right = 1,
        Middle = 2,
        Back = 3,
        Forward = 4,
    };
}

// Cursor shapes
namespace CursorShape {
    enum : int {
        Arrow = 0,
        IBeam = 1,
        Crosshair = 2,
        Hand = 3,
        HResize = 4,
        VResize = 5,
        NotAllowed = 6,
    };
}

// Gamepad buttons (Xbox layout)
namespace GamepadButton {
    enum : int {
        A = 0, B, X, Y,
        LB, RB,
        Back, Start, Guide,
        LeftStick, RightStick,
        DPUp, DPRight, DPDown, DPLeft,
    };
}

// Gamepad axes
namespace GamepadAxis {
    enum : int {
        LeftX = 0, LeftY,
        RightX, RightY,
        LeftTrigger, RightTrigger,
    };
}

} // namespace tensyoki
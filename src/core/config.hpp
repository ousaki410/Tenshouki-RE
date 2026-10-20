/**
 * config.hpp - configuration management (TOML-based)
 */

#pragma once

#include <toml++/toml.hpp>
#include <string>
#include <filesystem>
#include <optional>
#include <variant>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace tensyoki {

class Config {
public:
    // Get singleton
    static Config& instance() {
        static Config instance;
        return instance;
    }
    
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    // Load configuration file
    bool load(const std::string& filename = "config.toml");
    
    // Save configuration file
    bool save(const std::string& filename = "") const;
    
    // Get value (with type conversion support)
    template<typename T>
    T get(const std::string& key, const T& default_value = T{}) const {
        std::shared_lock lock(mutex_);
        auto it = values_.find(key);
        if (it != values_.end()) {
            try {
                return std::get<T>(it->second);
            } catch (const std::bad_variant_access&) {
                // Type mismatch, try conversion
                return convert<T>(it->second);
            }
        }
        return default_value;
    }
    
    // Overload: handles string literals
    std::string get(const std::string& key, const char* default_value) const {
        return get(key, std::string(default_value));
    }
    
    // Overload: handles string literals (non-const)
    std::string get(const std::string& key, std::string_view default_value) const {
        return get(key, std::string(default_value));
    }
    
    // Set value
    template<typename T>
    void set(const std::string& key, const T& value) {
        std::unique_lock lock(mutex_);
        values_[key] = value;
        dirty_ = true;
    }
    
    // Overload: handles string literals
    void set(const std::string& key, const char* value) {
        std::unique_lock lock(mutex_);
        values_[key] = std::string(value);
        dirty_ = true;
    }
    
    // Overload: handles string_view
    void set(const std::string& key, std::string_view value) {
        std::unique_lock lock(mutex_);
        values_[key] = std::string(value);
        dirty_ = true;
    }
    
    // Check whether key exists
    bool has(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return values_.contains(key);
    }
    
    // Remove key
    void remove(const std::string& key) {
        std::unique_lock lock(mutex_);
        values_.erase(key);
        dirty_ = true;
    }
    
    // Get all keys
    std::vector<std::string> keys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        result.reserve(values_.size());
        for (const auto& [k, v] : values_) result.push_back(k);
        return result;
    }
    
    // Window settings shortcuts
    int get_window_width() const { return get("window.width", 1280); }
    int get_window_height() const { return get("window.height", 720); }
    std::string get_window_title() const { return get("window.title", "Tensyoki Linux Port"); }
    bool get_fullscreen() const { return get("window.fullscreen", false); }
    bool get_vsync() const { return get("window.vsync", true); }
    int get_max_fps() const { return get("window.max_fps", 0); }
    
    // Data path
    std::filesystem::path get_data_root() const { 
        return get("paths.data_root", std::filesystem::path{}); 
    }
    void set_data_root(const std::filesystem::path& path) { 
        set("paths.data_root", path); 
    }
    
    // Language settings
    std::string get_language() const { return get("game.language", "ja"); }
    
    // Audio settings
    float get_master_volume() const { return get("audio.master_volume", 1.0f); }
    float get_music_volume() const { return get("audio.music_volume", 1.0f); }
    float get_sfx_volume() const { return get("audio.sfx_volume", 1.0f); }
    
    // Render settings
    std::string get_renderer_backend() const { return get("renderer.backend", "opengl"); }
    int get_msaa_samples() const { return get("renderer.msaa_samples", 0); }
    bool get_debug_overlay() const { return get("renderer.debug_overlay", false); }
    
    // Export as TOML string (for debugging)
    std::string to_toml_string() const;

private:
    Config() = default;
    
    using Value = std::variant<
        bool, int, int64_t, double, float, 
        std::string, std::filesystem::path
    >;
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Value> values_;
    mutable bool dirty_ = false;
    std::filesystem::path config_file_;
    
    // Recursively load from TOML table
    void load_table(const toml::table& tbl, const std::string& prefix = "");
    
    // Type conversion
    template<typename T>
    T convert(const Value& v) const {
        if constexpr (std::is_same_v<T, bool>) {
            if (std::holds_alternative<int>(v)) return std::get<int>(v) != 0;
            if (std::holds_alternative<std::string>(v)) {
                auto s = std::get<std::string>(v);
                return s == "true" || s == "1" || s == "yes" || s == "on";
            }
        } else if constexpr (std::is_same_v<T, int>) {
            if (std::holds_alternative<int64_t>(v)) return static_cast<int>(std::get<int64_t>(v));
            if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
            if (std::holds_alternative<float>(v)) return static_cast<int>(std::get<float>(v));
            if (std::holds_alternative<std::string>(v)) return std::stoi(std::get<std::string>(v));
        } else if constexpr (std::is_same_v<T, float>) {
            if (std::holds_alternative<double>(v)) return static_cast<float>(std::get<double>(v));
            if (std::holds_alternative<int>(v)) return static_cast<float>(std::get<int>(v));
            if (std::holds_alternative<int64_t>(v)) return static_cast<float>(std::get<int64_t>(v));
            if (std::holds_alternative<std::string>(v)) return std::stof(std::get<std::string>(v));
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (std::holds_alternative<std::filesystem::path>(v)) 
                return std::get<std::filesystem::path>(v).string();
            if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
            if (std::holds_alternative<double>(v)) return std::to_string(std::get<double>(v));
        } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
            if (std::holds_alternative<std::string>(v)) 
                return std::filesystem::path(std::get<std::string>(v));
        }
        return T{};
    }
    
    // Save to TOML table
    void save_table(toml::table& tbl) const;
    
    // Set default values
    void set_defaults();
};

} // namespace tensyoki
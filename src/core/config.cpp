/**
 * config.cpp - configuration management implementation
 */

#include "config.hpp"
#include "logging.hpp"
#include "filesystem.hpp"
#include <fstream>
#include <sstream>
#include <variant>

namespace tensyoki {

bool Config::load(const std::string& filename) {
    std::filesystem::path path = filename;
    if (!std::filesystem::exists(path)) {
        // Create default configuration
        set_defaults();
        return save(filename);
    }
    
    config_file_ = path;
    
    try {
        std::ifstream file(path);
        if (!file) {
            LOG_ERROR("Failed to open config file: {}", path.string());
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        toml::table tbl = toml::parse(content);
        load_table(tbl);
        
        LOG_INFO("Config loaded from: {}", path.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse config: {}", e.what());
        return false;
    }
}

bool Config::save(const std::string& filename) const {
    std::filesystem::path path = filename.empty() ? config_file_ : std::filesystem::path(filename);
    if (path.empty()) {
        LOG_ERROR("No config file path specified");
        return false;
    }
    
    try {
        toml::table tbl;
        save_table(tbl);
        
        std::ofstream file(path);
        if (!file) {
            LOG_ERROR("Failed to open config file for writing: {}", path.string());
            return false;
        }
        
        file << tbl;
        
        LOG_INFO("Config saved to: {}", path.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save config: {}", e.what());
        return false;
    }
}

void Config::load_table(const toml::table& tbl, const std::string& prefix) {
    for (const auto& [key, val] : tbl) {
        std::string key_str = std::string(key.str());
        std::string full_key = prefix.empty() ? key_str : prefix + "." + key_str;
        
        if (val.is_table()) {
            load_table(*val.as_table(), full_key);
        } else {
            // Convert toml::node to our variant type
            std::shared_lock lock(mutex_);
            if (val.is_string()) {
                values_[full_key] = std::string(val.as_string()->get());
            } else if (val.is_integer()) {
                values_[full_key] = static_cast<int>(val.as_integer()->get());
            } else if (val.is_floating_point()) {
                values_[full_key] = static_cast<float>(val.as_floating_point()->get());
            } else if (val.is_boolean()) {
                values_[full_key] = val.as_boolean()->get();
            }
            // Skip arrays and other types for now
        }
    }
}

void Config::save_table(toml::table& tbl) const {
    std::shared_lock lock(mutex_);
    
    for (const auto& [key, val] : values_) {
        // Parse key path (e.g., "window.width")
        std::vector<std::string> parts;
        std::string part;
        for (char c : key) {
            if (c == '.') {
                if (!part.empty()) {
                    parts.push_back(part);
                    part.clear();
                }
            } else {
                part += c;
            }
        }
        if (!part.empty()) parts.push_back(part);
        
        if (parts.empty()) continue;
        
        // Navigate/create nested tables
        toml::table* current = &tbl;
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            const std::string& part = parts[i];
            toml::node_view<toml::node> existing = (*current)[part];
            
            if (!existing) {
                current->insert_or_assign(part, toml::table{});
            }
            current = (*current)[part].as_table();
        }
        
        // Set the leaf value
        const std::string& leaf = parts.back();
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, int>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, double>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, float>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                current->insert_or_assign(leaf, v);
            } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
                current->insert_or_assign(leaf, v.string());
            }
        }, val);
    }
}

void Config::set_defaults() {
    // Window settings
    set("window.width", 1280);
    set("window.height", 720);
    set("window.title", "Tensyoki Linux Port");
    set("window.fullscreen", false);
    set("window.vsync", true);
    set("window.max_fps", 0);
    
    // Paths
    set("paths.data_root", std::filesystem::path{});
    set("paths.save_dir", std::filesystem::path{});
    
    // Game
    set("game.language", "ja");
    set("game.difficulty", "normal");
    
    // Audio
    set("audio.master_volume", 1.0f);
    set("audio.music_volume", 1.0f);
    set("audio.sfx_volume", 1.0f);
    set("audio.enabled", true);
    
    // Render
    set("renderer.backend", "opengl");
    set("renderer.msaa_samples", 0);
    set("renderer.debug_overlay", false);
    set("renderer.anisotropy", 16);
    
    // Input
    set("input.mouse_sensitivity", 1.0f);
    set("input.invert_mouse", false);
}

std::string Config::to_toml_string() const {
    toml::table tbl;
    save_table(tbl);
    std::ostringstream oss;
    oss << tbl;
    return oss.str();
}

} // namespace tensyoki
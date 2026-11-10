/**
 * gt1g_texture_cache.hpp - Cache for GT1G-decoded OpenGL textures
 *
 * Decodes GT1G blocks on demand and uploads the resulting RGBA8888 pixels
 * to OpenGL via Renderer::Texture. Entries are keyed by (container, entry
 * index, texture index) so the same texture is decoded and uploaded once.
 */

#pragma once

#include "render/renderer.hpp"
#include "bin_parser.hpp"
#include "gt1g_decoder.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>

namespace tensyoki {

struct GT1GTextureKey {
    std::string container;
    uint32_t entry_index = 0;
    uint32_t texture_index = 0;

    bool operator==(const GT1GTextureKey&) const = default;
};

struct GT1GTextureKeyHash {
    size_t operator()(const GT1GTextureKey& k) const noexcept {
        size_t h = std::hash<std::string>{}(k.container);
        h ^= std::hash<uint32_t>{}(k.entry_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.texture_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/// Decodes GT1G blocks and caches the resulting OpenGL textures
class GT1GTextureCache {
public:
    GT1GTextureCache() = default;
    ~GT1GTextureCache() = default;

    GT1GTextureCache(const GT1GTextureCache&) = delete;
    GT1GTextureCache& operator=(const GT1GTextureCache&) = delete;

    /// Decode (if needed) and return an OpenGL texture for the given GT1G data entry.
    /// `container` is used for logging/keying only (e.g. "res_grp.bin").
    std::shared_ptr<Renderer::Texture> get_or_load(
        Renderer& renderer,
        const std::string& container,
        const BinArchive& archive,
        uint32_t entry_index);

    /// Decode all GT1G textures from a nested LINK entry and return them.
    std::vector<std::shared_ptr<Renderer::Texture>> load_all_from_nested(
        Renderer& renderer,
        const std::string& container,
        const BinArchive& archive,
        uint32_t entry_index);

    size_t cached_count() const noexcept { return cache_.size(); }

private:
    std::unordered_map<GT1GTextureKey, std::shared_ptr<Renderer::Texture>, GT1GTextureKeyHash> cache_;
    mutable std::mutex mutex_;
};

} // namespace tensyoki

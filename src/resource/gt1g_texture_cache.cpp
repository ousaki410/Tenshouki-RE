/**
 * gt1g_texture_cache.cpp - GT1G texture cache implementation
 */

#include "gt1g_texture_cache.hpp"
#include "core/logging.hpp"
#include <algorithm>

namespace tensyoki {

std::shared_ptr<Renderer::Texture> GT1GTextureCache::get_or_load(
    Renderer& renderer,
    const std::string& container,
    const BinArchive& archive,
    uint32_t entry_index) {

    GT1GTextureKey key{container, entry_index, 0};  // texture 0 of the entry

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    // Find the nested LINK entry
    const auto* entry = archive.find_by_index(entry_index);
    if (!entry || !entry->is_nested_link) {
        LOG_WARN("GT1GTextureCache: entry {} of {} is not a nested LINK",
                 entry_index, container);
        return nullptr;
    }

    auto nested = archive.parse_nested(*entry);
    if (!nested) {
        LOG_WARN("GT1GTextureCache: failed to parse nested LINK {} of {}: {}",
                 entry_index, container, to_string(nested.error()));
        return nullptr;
    }

    // Find the first GT1G data block
    size_t gt1g_block_index = 0;
    bool found = false;
    for (size_t i = 0; i < nested->data_entries.size(); ++i) {
        if (nested->data_entries[i].signature == "GT1G") {
            gt1g_block_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        LOG_WARN("GT1GTextureCache: no GT1G data block in entry {} of {}",
                 entry_index, container);
        return nullptr;
    }

    const auto& block_entry = nested->data_entries[gt1g_block_index];

    // View the nested payload and extract the GT1G block bytes
    auto payload_result = archive.view(*entry);
    if (!payload_result) {
        LOG_WARN("GT1GTextureCache: failed to view entry {} of {}",
                 entry_index, container);
        return nullptr;
    }
    auto payload = *payload_result;

    if (block_entry.offset + block_entry.size > payload.size()) {
        LOG_WARN("GT1GTextureCache: GT1G block out of bounds in entry {} of {}",
                 entry_index, container);
        return nullptr;
    }

    auto gt1g_block = payload.subspan(block_entry.offset, block_entry.size);
    auto decoded = decode_gt1g_block(gt1g_block);
    if (!decoded.error.empty() || decoded.textures.empty()) {
        LOG_WARN("GT1GTextureCache: failed to decode GT1G block in entry {} of {}: {}",
                 entry_index, container, decoded.error);
        return nullptr;
    }

    // Upload texture 0 (the first texture in the block)
    const auto& tex = decoded.textures[0];
    if (tex.pixels.empty()) {
        LOG_WARN("GT1GTextureCache: decoded texture 0 has no pixels");
        return nullptr;
    }

    Renderer::TextureCreateInfo info{};
    info.width = tex.width;
    info.height = tex.height;
    info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
    info.initial_data = tex.pixels.data();
    info.data_size = tex.pixels.size();

    auto gl_tex = renderer.create_texture(info);
    if (!gl_tex) {
        LOG_WARN("GT1GTextureCache: failed to create OpenGL texture for entry {} of {}",
                 entry_index, container);
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = gl_tex;
    }

    LOG_INFO("GT1GTextureCache: loaded {}x{} texture from {} entry {}",
             tex.width, tex.height, container, entry_index);
    return gl_tex;
}

std::vector<std::shared_ptr<Renderer::Texture>> GT1GTextureCache::load_all_from_nested(
    Renderer& renderer,
    const std::string& container,
    const BinArchive& archive,
    uint32_t entry_index) {

    std::vector<std::shared_ptr<Renderer::Texture>> textures;

    const auto* entry = archive.find_by_index(entry_index);
    if (!entry || !entry->is_nested_link) {
        return textures;
    }

    auto nested = archive.parse_nested(*entry);
    if (!nested) {
        return textures;
    }

    auto payload_result = archive.view(*entry);
    if (!payload_result) {
        return textures;
    }
    auto payload = *payload_result;

    for (size_t bi = 0; bi < nested->data_entries.size(); ++bi) {
        const auto& block_entry = nested->data_entries[bi];
        if (block_entry.signature != "GT1G") continue;

        if (block_entry.offset + block_entry.size > payload.size()) continue;

        auto gt1g_block = payload.subspan(block_entry.offset, block_entry.size);
        auto decoded = decode_gt1g_block(gt1g_block);
        if (!decoded.error.empty() || decoded.textures.empty()) continue;

        for (size_t ti = 0; ti < decoded.textures.size(); ++ti) {
            const auto& t = decoded.textures[ti];
            if (t.pixels.empty()) continue;

            GT1GTextureKey key{container, entry_index, static_cast<uint32_t>(ti)};
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = cache_.find(key);
                if (it != cache_.end()) {
                    textures.push_back(it->second);
                    continue;
                }
            }

            Renderer::TextureCreateInfo info{};
            info.width = t.width;
            info.height = t.height;
            info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
            info.initial_data = t.pixels.data();
            info.data_size = t.pixels.size();

            auto gl_tex = renderer.create_texture(info);
            if (!gl_tex) continue;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                cache_[key] = gl_tex;
            }
            textures.push_back(gl_tex);
        }
    }

    return textures;
}

} // namespace tensyoki

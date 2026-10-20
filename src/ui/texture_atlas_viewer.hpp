/**
 * texture_atlas_viewer.hpp - Texture atlas preview tool (ImGui)
 * For browsing extracted GT1G texture atlases with zoom, pan, and UV coordinate display
 */

#pragma once

#include "render/renderer.hpp"
#include "ui/imgui_backend.hpp"
#include <string>
#include <filesystem>
#include <vector>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace tensyoki {

struct TextureAtlasInfo {
    std::string name;
    std::filesystem::path file_path;
    uint32_t width = 0;
    uint32_t height = 0;
    std::shared_ptr<Renderer::Texture> texture;
    bool loaded = false;
};

class TextureAtlasViewer {
public:
    TextureAtlasViewer(Renderer& renderer) : renderer_(renderer) {}

    // Load texture atlas
    bool load_atlas(const std::filesystem::path& path, const std::string& name = "") {
        if (!std::filesystem::exists(path)) {
            LOG_ERROR("Texture file not found: {}", path.string());
            return false;
        }

        // Load with stb_image
        int w, h, channels;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (!pixels) {
            LOG_ERROR("Failed to load image: {}", path.string());
            return false;
        }

        Renderer::TextureCreateInfo tex_info{};
        tex_info.width = static_cast<uint32_t>(w);
        tex_info.height = static_cast<uint32_t>(h);
        tex_info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
        tex_info.initial_data = pixels;
        tex_info.data_size = static_cast<size_t>(w) * h * 4;
        tex_info.mip_levels = 1;

        auto texture = renderer_.create_texture(tex_info);
        stbi_image_free(pixels);

        if (!texture) {
            LOG_ERROR("Failed to create texture from: {}", path.string());
            return false;
        }

        TextureAtlasInfo info;
        info.name = name.empty() ? std::filesystem::path(path).stem().string() : name;
        info.file_path = path;
        info.width = static_cast<uint32_t>(w);
        info.height = static_cast<uint32_t>(h);
        info.texture = texture;
        info.loaded = true;

        atlases_.push_back(std::move(info));
        current_atlas_ = atlases_.size() - 1;
        return true;
    }

    // ImGui drawing
    void draw() {
        if (atlases_.empty()) {
            ImGui::Text("No atlas loaded. Use load_atlas() first.");
            return;
        }

        ImGui::Begin("Texture Atlas Viewer", nullptr, ImGuiWindowFlags_MenuBar);

        // Menu Bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load Atlas...")) {
                    // TODO: file dialog
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close")) {
                    show_ = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show UV Grid", nullptr, &show_uv_grid_);
                ImGui::MenuItem("Show Pixel Info", nullptr, &show_pixel_info_);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Atlas selector
        if (atlases_.size() > 1) {
            const char* preview = atlases_[current_atlas_].name.c_str();
            if (ImGui::BeginCombo("Atlas", preview)) {
                for (size_t i = 0; i < atlases_.size(); ++i) {
                    bool selected = (current_atlas_ == i);
                    if (ImGui::Selectable(atlases_[i].name.c_str(), selected)) {
                        current_atlas_ = i;
                    }
                }
                ImGui::EndCombo();
            }
        }

        auto& atlas = atlases_[current_atlas_];
        if (!atlas.loaded || !atlas.texture) {
            ImGui::Text("Atlas not loaded properly");
            ImGui::End();
            return;
        }

        ImGui::Separator();

        // Info bar
        ImGui::Text("Atlas: %s (%ux%u)", atlas.name.c_str(), atlas.width, atlas.height);
        ImGui::SameLine();
        ImGui::Text("Path: %s", atlas.file_path.string().c_str());
        ImGui::Separator();

        // Control panel
        ImGui::BeginChild("Controls", ImVec2(0, 100), true);
        ImGui::DragFloat("Zoom", &zoom_, 0.01f, 0.1f, 10.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button("Reset Zoom")) zoom_ = 1.0f;
        ImGui::SameLine();
        ImGui::Checkbox("UV Grid", &show_uv_grid_);
        ImGui::SameLine();
        ImGui::Checkbox("Pixel Info", &show_pixel_info_);
        ImGui::SameLine();
        if (ImGui::Button("Fit to Window")) {
            fit_to_window();
        }
        ImGui::EndChild();

        // Image display area
        ImGui::Separator();
        ImGui::BeginChild("ImageView", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        auto tex_id = static_cast<ImTextureID>(atlas.texture->get_id());
        ImVec2 img_size(static_cast<float>(atlas.width) * zoom_, static_cast<float>(atlas.height) * zoom_);

        // Compute centering offset
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 offset((avail.x - img_size.x) * 0.5f, (avail.y - img_size.y) * 0.5f);
        if (offset.x < 0) offset.x = 0;
        if (offset.y < 0) offset.y = 0;

        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImVec2 img_min = ImVec2(cursor_pos.x + offset.x, cursor_pos.y + offset.y);
        ImVec2 img_max = ImVec2(img_min.x + img_size.x, img_min.y + img_size.y);

        // Draw the image
        ImGui::GetWindowDrawList()->AddImage(tex_id, img_min, img_max, ImVec2(0, 0), ImVec2(1, 1));

        // UV grid
        if (show_uv_grid_) {
            draw_uv_grid(img_min, img_max, atlas.width, atlas.height);
        }

        // Pixel info
        if (show_pixel_info_) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            if (mouse_pos.x >= img_min.x && mouse_pos.x < img_max.x &&
                mouse_pos.y >= img_min.y && mouse_pos.y < img_max.y) {
                float u = (mouse_pos.x - img_min.x) / img_size.x;
                float v = (mouse_pos.y - img_min.y) / img_size.y;
                uint32_t px = static_cast<uint32_t>(u * atlas.width);
                uint32_t py = static_cast<uint32_t>(v * atlas.height);
                ImGui::SetTooltip("UV: (%.4f, %.4f)\nPixel: (%u, %u)\nSize: %ux%u",
                                 u, v, px, py, atlas.width, atlas.height);
            }
        }

        ImGui::EndChild();
        ImGui::End();
    }

    void set_visible(bool show) { show_ = show; }
    bool is_visible() const { return show_; }

private:
    void draw_uv_grid(ImVec2 img_min, ImVec2 img_max, uint32_t width, uint32_t height) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const uint32_t grid_step = 64; // UV grid step (pixels)

        ImU32 grid_color = IM_COL32(255, 255, 255, 50);
        ImU32 text_color = IM_COL32(255, 255, 255, 180);

        float img_w = static_cast<float>(atlases_.empty() ? 1 : atlases_[current_atlas_].width);
        float img_h = static_cast<float>(atlases_.empty() ? 1 : atlases_[current_atlas_].height);

        // Vertical lines
        for (uint32_t x = 0; x <= width; x += 64) {
            float u = static_cast<float>(x) / img_w;
            float x_pos = img_min.x + u * (img_max.x - img_min.x);
            draw_list->AddLine(ImVec2(x_pos, img_min.y), ImVec2(x_pos, img_max.y), IM_COL32(255, 255, 255, 50));
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", x);
            draw_list->AddText(ImVec2(x_pos + 2, img_min.y + 2), IM_COL32(255, 255, 255, 180), buf);
        }

        // Horizontal lines
        for (uint32_t y = 0; y <= height; y += 64) {
            float v = static_cast<float>(y) / img_h;
            float y_pos = img_min.y + v * (img_max.y - img_min.y);
            draw_list->AddLine(ImVec2(img_min.x, y_pos), ImVec2(img_max.x, y_pos), IM_COL32(255, 255, 255, 50));
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", y);
            draw_list->AddText(ImVec2(img_min.x + 2, y_pos + 2), IM_COL32(255, 255, 255, 180), buf);
        }
    }

    void fit_to_window() {
        if (atlases_.empty() || current_atlas_ >= atlases_.size()) return;
        auto& atlas = atlases_[current_atlas_];
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float zoom_x = avail.x / static_cast<float>(atlas.width);
        float zoom_y = avail.y / static_cast<float>(atlas.height);
        zoom_ = std::min(zoom_x, zoom_y) * 0.95f; // leave 5% margin
    }

    Renderer& renderer_;
    std::vector<TextureAtlasInfo> atlases_;
    size_t current_atlas_ = 0;
    float zoom_ = 1.0f;
    bool show_uv_grid_ = true;
    bool show_pixel_info_ = true;
    bool show_ = true;
};

} // namespace tensyoki

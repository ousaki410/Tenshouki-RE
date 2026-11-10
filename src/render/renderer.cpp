/**
 * renderer.cpp - OpenGL 4.5 renderer implementation (minimal viable implementation)
 */

#include "renderer.hpp"
#include "core/logging.hpp"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cmath>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <cstring>

namespace tensyoki {

// === Shader Helper ===
static GLuint compile_shader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar info_log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, info_log);
        LOG_ERROR("Shader compilation failed: {}", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar info_log[1024];
        glGetProgramInfoLog(program, 1024, nullptr, info_log);
        LOG_ERROR("Program linking failed: {}", info_log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// === Renderer Impl ===
struct Renderer::Impl {
    Backend backend_ = Backend::OpenGL;
    int width_ = 1280;
    int height_ = 720;
    bool vsync_ = true;
    Stats stats_;
    GLuint default_vao_ = 0;
    GLuint blit_program_ = 0;
    GLuint blit_vao_ = 0;
    GLuint blit_vbo_ = 0;

    Impl() = default;
    ~Impl() {
        if (blit_program_) glDeleteProgram(blit_program_);
        if (blit_vao_) glDeleteVertexArrays(1, &blit_vao_);
        if (blit_vbo_) glDeleteBuffers(1, &blit_vbo_);
        if (default_vao_) glDeleteVertexArrays(1, &default_vao_);
    }

    bool init_opengl(const InitParams& params) {
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            LOG_ERROR("Failed to initialize GLAD");
            return false;
        }

        LOG_INFO("OpenGL Vendor: {}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        LOG_INFO("OpenGL Renderer: {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        LOG_INFO("OpenGL Version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        LOG_INFO("GLSL Version: {}", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

        if (params.debug_overlay) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback([](GLenum, GLenum, GLuint, GLenum severity,
                                      GLsizei, const GLchar* message, const void*) {
                if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
                LOG_DEBUG("OpenGL Debug: {}", message);
            }, nullptr);
        }

        glGenVertexArrays(1, &default_vao_);
        glBindVertexArray(default_vao_);

        const char* vs_src = R"(
            #version 450 core
            layout(location = 0) in vec2 a_pos;
            layout(location = 1) in vec2 a_uv;
            out vec2 v_uv;
            void main() {
                gl_Position = vec4(a_pos, 0.0, 1.0);
                v_uv = a_uv;
            }
        )";
        const char* fs_src = R"(
            #version 450 core
            in vec2 v_uv;
            out vec4 out_color;
            layout(binding = 0) uniform sampler2D u_texture;
            void main() {
                out_color = texture(u_texture, v_uv);
            }
        )";
        GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        if (vs && fs) {
            blit_program_ = link_program(vs, fs);
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);

        float vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        glGenVertexArrays(1, &blit_vao_);
        glGenBuffers(1, &blit_vbo_);
        glBindVertexArray(blit_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, blit_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        return true;
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

bool Renderer::init(const InitParams& params) {
    impl_->backend_ = params.backend;
    impl_->width_ = params.width;
    impl_->height_ = params.height;
    impl_->vsync_ = params.vsync;

    if (params.backend == Backend::OpenGL || params.backend == Backend::Auto) {
        return impl_->init_opengl(params);
    }

    LOG_ERROR("Vulkan backend not yet implemented");
    return false;
}

void Renderer::shutdown() {
    // Recreate an empty Impl so other member functions do not dereference a null pointer after shutdown
    impl_ = std::make_unique<Impl>();
}

void Renderer::on_resize(int width, int height) {
    impl_->width_ = width;
    impl_->height_ = height;
    glViewport(0, 0, width, height);
}

void Renderer::begin_frame() {
    glViewport(0, 0, impl_->width_, impl_->height_);
    impl_->stats_ = Stats{};
}

void Renderer::end_frame() {
}

void Renderer::set_viewport(const Viewport& vp) {
    glViewport(vp.x, vp.y, vp.width, vp.height);
    glDepthRange(vp.min_depth, vp.max_depth);
}

void Renderer::set_scissor(int x, int y, int width, int height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

void Renderer::clear(float r, float g, float b, float a, float depth, uint32_t stencil) {
    glClearColor(r, g, b, a);
    glClearDepth(depth);
    glClearStencil(static_cast<GLint>(stencil));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

std::shared_ptr<Renderer::Texture> Renderer::create_texture(const TextureCreateInfo& info) {
    auto texture = std::make_shared<Texture>();
    if (texture->create(info)) {
        return texture;
    }
    return nullptr;
}

std::shared_ptr<Renderer::Buffer> Renderer::create_buffer(const BufferCreateInfo& info) {
    auto buffer = std::make_shared<Buffer>();
    if (buffer->create(info)) {
        return buffer;
    }
    return nullptr;
}

std::shared_ptr<Renderer::Pipeline> Renderer::create_pipeline(const PipelineCreateInfo& info) {
    auto pipeline = std::make_shared<Pipeline>();
    if (pipeline->create(info)) {
        return pipeline;
    }
    return nullptr;
}

std::shared_ptr<Renderer::Shader> Renderer::create_shader(ShaderStage, const std::vector<uint32_t>&) {
    // Minimal implementation: requires SPIR-V to GLSL conversion, not yet implemented
    return nullptr;
}

void Renderer::bind_pipeline(std::shared_ptr<Pipeline>) {
    // TODO: implement
}

void Renderer::bind_vertex_buffer(uint32_t, std::shared_ptr<Buffer>, uint32_t) {
    // TODO: implement
}

void Renderer::bind_index_buffer(std::shared_ptr<Buffer>, uint32_t) {
    // TODO: implement
}

void Renderer::bind_texture(uint32_t slot, std::shared_ptr<Texture> texture) {
    if (texture) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(texture->get_target(), texture->get_id());
        impl_->stats_.texture_binds++;
    }
}

void Renderer::bind_uniform_buffer(uint32_t slot, std::shared_ptr<Buffer> buffer, uint32_t offset, uint32_t size) {
    if (buffer) {
        GLsizeiptr range = size ? static_cast<GLsizeiptr>(size)
                                : static_cast<GLsizeiptr>(buffer->get_size()) - static_cast<GLsizeiptr>(offset);
        glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffer->get_id(), static_cast<GLintptr>(offset), range);
    }
}

void Renderer::draw(const DrawCall& call) {
    glDrawArraysInstancedBaseInstance(GL_TRIANGLES,
                                      static_cast<GLint>(call.first_vertex),
                                      static_cast<GLsizei>(call.vertex_count),
                                      static_cast<GLsizei>(call.instance_count),
                                      call.first_instance);
    impl_->stats_.draw_calls++;
    impl_->stats_.vertices += call.vertex_count;
    impl_->stats_.triangles += call.vertex_count / 3;
}

void Renderer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES,
                                                  static_cast<GLsizei>(index_count),
                                                  GL_UNSIGNED_INT,
                                                  reinterpret_cast<const void*>(static_cast<uintptr_t>(first_index) * sizeof(uint32_t)),
                                                  static_cast<GLsizei>(instance_count),
                                                  vertex_offset, first_instance);
    impl_->stats_.draw_calls++;
    impl_->stats_.triangles += index_count / 3;
}

void Renderer::draw_indirect(std::shared_ptr<Buffer>, uint32_t, uint32_t, uint32_t) {
    // TODO: implement
}

void Renderer::dispatch_compute(uint32_t, uint32_t, uint32_t) {
    // TODO: implement
}

Renderer::Backend Renderer::get_backend() const {
    return impl_->backend_;
}

void* Renderer::get_device() const {
    return nullptr; // OpenGL has no device handle
}

std::string Renderer::get_backend_name() const {
    switch (impl_->backend_) {
        case Backend::OpenGL: return "OpenGL 4.5";
        case Backend::Vulkan: return "Vulkan";
        default: return "Unknown";
    }
}

int Renderer::get_width() const { return impl_->width_; }
int Renderer::get_height() const { return impl_->height_; }

void Renderer::push_debug_group(const std::string& name) {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(name.size()), name.c_str());
}

void Renderer::pop_debug_group() {
    glPopDebugGroup();
}

void Renderer::insert_debug_marker(const std::string& marker) {
    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                         GL_DEBUG_SEVERITY_NOTIFICATION, static_cast<GLsizei>(marker.size()), marker.c_str());
}

bool Renderer::capture_screenshot(const std::filesystem::path&) {
    // TODO: implement screenshot
    return false;
}

Renderer::Stats Renderer::get_stats() const {
    return impl_->stats_;
}

void Renderer::reset_stats() {
    impl_->stats_ = Stats{};
}

// === SpriteRenderer Implementation ===
// Note: SpriteRenderer is a nested class of Renderer, so the fully qualified name is required
struct Renderer::SpriteRenderer::Impl {
    static constexpr size_t kFloatsPerVertex = 8;
    static constexpr size_t kVerticesPerSprite = 6;

    Renderer& renderer_;
    std::shared_ptr<Renderer::Texture> default_texture_;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    size_t max_sprites_ = 10000;
    size_t sprite_count_ = 0;
    std::vector<float> vertex_data_;
    glm::mat4 projection_{1.0f};

    explicit Impl(Renderer& r) : renderer_(r) {
        vertex_data_.reserve(max_sprites_ * kVerticesPerSprite * kFloatsPerVertex);
    }

    ~Impl() {
        if (program_) glDeleteProgram(program_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
    }

    bool init() {
        const char* vs_src = R"(
            #version 450 core
            layout(location = 0) in vec2 a_pos;
            layout(location = 1) in vec2 a_uv;
            layout(location = 2) in vec4 a_color;
            out vec2 v_uv;
            out vec4 v_color;
            uniform mat4 u_projection;
            void main() {
                gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
                v_uv = a_uv;
                v_color = a_color;
            }
        )";
        const char* fs_src = R"(
            #version 450 core
            in vec2 v_uv;
            in vec4 v_color;
            out vec4 out_color;
            layout(binding = 0) uniform sampler2D u_texture;
            void main() {
                out_color = texture(u_texture, v_uv) * v_color;
            }
        )";
        GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        if (vs && fs) {
            program_ = link_program(vs, fs);
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);

        if (!program_) return false;

        const GLsizeiptr buffer_bytes = static_cast<GLsizeiptr>(
            max_sprites_ * kVerticesPerSprite * kFloatsPerVertex * sizeof(float));

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, buffer_bytes, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
        glBindVertexArray(0);

        Renderer::TextureCreateInfo tex_info{};
        tex_info.width = 1;
        tex_info.height = 1;
        tex_info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
        uint8_t white[4] = {255, 255, 255, 255};
        tex_info.initial_data = white;
        tex_info.data_size = 4;
        default_texture_ = renderer_.create_texture(tex_info);

        return true;
    }

    void flush() {
        if (sprite_count_ == 0) return;

        glUseProgram(program_);
        glUniformMatrix4fv(glGetUniformLocation(program_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection_));

        if (default_texture_) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(default_texture_->get_target(), default_texture_->get_id());
        }

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(vertex_data_.size() * sizeof(float)),
                        vertex_data_.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sprite_count_ * kVerticesPerSprite));
        glBindVertexArray(0);

        vertex_data_.clear();
        sprite_count_ = 0;
    }
};

Renderer::SpriteRenderer::SpriteRenderer(Renderer& renderer)
    : impl_(std::make_unique<Impl>(renderer)) {
    if (!impl_->init()) {
        LOG_ERROR("SpriteRenderer initialization failed");
    }
}

Renderer::SpriteRenderer::~SpriteRenderer() = default;

void Renderer::SpriteRenderer::begin(const glm::mat4& projection) {
    impl_->projection_ = projection;
    impl_->sprite_count_ = 0;
    impl_->vertex_data_.clear();
}

void Renderer::SpriteRenderer::draw(const Sprite& sprite) {
    if (impl_->sprite_count_ >= impl_->max_sprites_) {
        impl_->flush();
    }

    auto tex = sprite.texture ? sprite.texture : impl_->default_texture_;
    if (!tex) return;

    float w = sprite.size.x;
    float h = sprite.size.y;
    float ox = w * sprite.origin.x;
    float oy = h * sprite.origin.y;
    float c = std::cos(sprite.rotation);
    float s = std::sin(sprite.rotation);

    float x1 = -ox, y1 = -oy;
    float x2 = w - ox, y2 = -oy;
    float x3 = w - ox, y3 = h - oy;
    float x4 = -ox, y4 = h - oy;

    auto rot = [&](float x, float y) -> std::pair<float, float> {
        return {x * c - y * s, x * s + y * c};
    };

    auto [rx1, ry1] = rot(x1, y1);
    auto [rx2, ry2] = rot(x2, y2);
    auto [rx3, ry3] = rot(x3, y3);
    auto [rx4, ry4] = rot(x4, y4);

    float px = sprite.position.x;
    float py = sprite.position.y;

    float u0 = sprite.uv_rect.x;
    float v0 = sprite.uv_rect.y;
    float u1 = sprite.uv_rect.z;
    float v1 = sprite.uv_rect.w;

    const float cr = sprite.color.r, cg = sprite.color.g, cb = sprite.color.b, ca = sprite.color.a;
    float verts[] = {
        rx1 + px, ry1 + py, u0, v0, cr, cg, cb, ca,
        rx2 + px, ry2 + py, u1, v0, cr, cg, cb, ca,
        rx3 + px, ry3 + py, u1, v1, cr, cg, cb, ca,
        rx1 + px, ry1 + py, u0, v0, cr, cg, cb, ca,
        rx3 + px, ry3 + py, u1, v1, cr, cg, cb, ca,
        rx4 + px, ry4 + py, u0, v1, cr, cg, cb, ca,
    };

    impl_->vertex_data_.insert(impl_->vertex_data_.end(), std::begin(verts), std::end(verts));
    impl_->sprite_count_++;
}

void Renderer::SpriteRenderer::draw_batch(const std::vector<Sprite>& sprites) {
    for (const auto& s : sprites) draw(s);
}

void Renderer::SpriteRenderer::end() {
    impl_->flush();
}

void Renderer::SpriteRenderer::flush() {
    impl_->flush();
}

void Renderer::SpriteRenderer::set_default_texture(std::shared_ptr<Texture> tex) {
    impl_->default_texture_ = std::move(tex);
}

// === TextRenderer Implementation (FreeType 2 + SpriteRenderer) ===

namespace {

constexpr int kAtlasWidth = 1024;
constexpr int kAtlasHeight = 1024;
constexpr uint32_t kFirstChar = 0x20;  // space
constexpr uint32_t kLastChar = 0x7E;   // tilde

/// Decode a UTF-8 string into a vector of Unicode codepoints.
std::vector<uint32_t> decode_utf8(const std::string& s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint8_t lead = static_cast<uint8_t>(s[i]);
        uint32_t cp = 0;
        int seq_len = 1;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F; seq_len = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F; seq_len = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07; seq_len = 4;
        } else {
            // Invalid lead byte: skip
            ++i;
            continue;
        }
        bool ok = true;
        for (int j = 1; j < seq_len; ++j) {
            if (i + j >= s.size()) { ok = false; break; }
            uint8_t cont = static_cast<uint8_t>(s[i + j]);
            if ((cont & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cont & 0x3F);
        }
        if (!ok) { ++i; continue; }
        out.push_back(cp);
        i += seq_len;
    }
    return out;
}

/// Per-glyph layout information (computed during the layout pass).
struct LayoutGlyph {
    uint32_t codepoint = 0;
    float x = 0;       // cursor x before this glyph (pre-alignment)
    float y = 0;       // line y (pre-alignment)
    float advance = 0; // advance in pixels at current scale
    float bearing_x = 0;
    float bearing_y = 0;
    float width = 0;
    float height = 0;
    glm::vec4 uv_rect = {0, 0, 1, 1};
};

/// One line of laid-out text.
struct LayoutLine {
    float width = 0;              // total advance width of the line
    std::vector<LayoutGlyph> glyphs;
};

} // namespace

struct GlyphInfo {
    uint32_t codepoint = 0;
    glm::vec4 uv_rect = {0, 0, 1, 1};  // UV in atlas
    glm::vec2 size = {0, 0};           // pixel size
    glm::vec2 bearing = {0, 0};        // offset from baseline
    int32_t advance = 0;               // advance in 1/64 pixels
};

struct Renderer::TextRenderer::Impl {
    Renderer& renderer_;
    FT_Library ft_library_ = nullptr;
    FT_Face ft_face_ = nullptr;
    GLuint atlas_texture_id_ = 0;
    int atlas_width_ = kAtlasWidth;
    int atlas_height_ = kAtlasHeight;
    int atlas_cursor_x_ = 0;
    int atlas_cursor_y_ = 0;
    int atlas_row_height_ = 0;
    float font_size_ = 24.0f;
    std::vector<uint8_t> atlas_pixels_;  // alpha channel only, expanded to RGBA on upload
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    std::unique_ptr<Renderer::SpriteRenderer> sprite_renderer_;
    std::shared_ptr<Renderer::Texture> atlas_texture_obj_;
    bool atlas_dirty_ = false;

    explicit Impl(Renderer& r) : renderer_(r) {
        if (FT_Init_FreeType(&ft_library_) != 0) {
            LOG_ERROR("FreeType: failed to initialize library");
        }
        sprite_renderer_ = std::make_unique<Renderer::SpriteRenderer>(renderer_);
        // Allocate alpha-only atlas buffer
        atlas_pixels_.assign(static_cast<size_t>(atlas_width_) * atlas_height_, 0);
    }

    ~Impl() {
        if (atlas_texture_id_) glDeleteTextures(1, &atlas_texture_id_);
        if (ft_face_) FT_Done_Face(ft_face_);
        if (ft_library_) FT_Done_FreeType(ft_library_);
    }

    bool load_font(const std::filesystem::path& path, float size) {
        if (!ft_library_) return false;
        if (ft_face_) FT_Done_Face(ft_face_);
        ft_face_ = nullptr;

        if (FT_New_Face(ft_library_, path.string().c_str(), 0, &ft_face_) != 0) {
            LOG_ERROR("FreeType: failed to load font: {}", path.string());
            return false;
        }

        FT_Set_Pixel_Sizes(ft_face_, 0, static_cast<FT_UInt>(size));
        font_size_ = size;

        // Reset atlas
        atlas_cursor_x_ = 0;
        atlas_cursor_y_ = 0;
        atlas_row_height_ = 0;
        std::fill(atlas_pixels_.begin(), atlas_pixels_.end(), 0);
        glyphs_.clear();
        atlas_dirty_ = true;

        // Pre-render ASCII characters
        for (uint32_t cp = kFirstChar; cp <= kLastChar; ++cp) {
            if (FT_Load_Char(ft_face_, cp, FT_LOAD_RENDER) != 0) continue;
            FT_Bitmap& bm = ft_face_->glyph->bitmap;
            if (bm.width == 0 || bm.rows == 0) {
                GlyphInfo info;
                info.codepoint = cp;
                info.size = {0, 0};
                info.bearing = {0, 0};
                info.advance = ft_face_->glyph->advance.x;
                info.uv_rect = {0, 0, 0, 0};
                glyphs_[cp] = info;
                continue;
            }

            glm::vec4 uv;
            if (!pack_glyph(bm, bm.width, bm.rows, uv)) {
                LOG_WARN("FreeType: atlas full, skipping codepoint U+{:04X}", cp);
                continue;
            }

            GlyphInfo info;
            info.codepoint = cp;
            info.size = {static_cast<float>(bm.width), static_cast<float>(bm.rows)};
            info.bearing = {static_cast<float>(ft_face_->glyph->bitmap_left),
                            static_cast<float>(ft_face_->glyph->bitmap_top)};
            info.advance = ft_face_->glyph->advance.x;
            info.uv_rect = uv;
            glyphs_[cp] = info;
        }

        upload_atlas();
        return true;
    }

    bool pack_glyph(const FT_Bitmap& bm, int width, int rows, glm::vec4& uv) {
        // Simple row-based packing
        if (atlas_cursor_x_ + width > atlas_width_) {
            atlas_cursor_x_ = 0;
            atlas_cursor_y_ += atlas_row_height_;
            atlas_row_height_ = 0;
        }
        if (atlas_cursor_y_ + rows > atlas_height_) {
            return false;  // atlas full
        }

        // Copy alpha pixels into atlas (alpha channel only)
        for (int row = 0; row < rows; ++row) {
            size_t dst = static_cast<size_t>(atlas_cursor_y_ + row) * atlas_width_ + atlas_cursor_x_;
            const uint8_t* src = bm.buffer + static_cast<size_t>(row) * bm.pitch;
            for (int col = 0; col < width; ++col) {
                atlas_pixels_[dst + col] = src[col];
            }
        }

        float u0 = static_cast<float>(atlas_cursor_x_) / atlas_width_;
        float v0 = static_cast<float>(atlas_cursor_y_) / atlas_height_;
        float u1 = static_cast<float>(atlas_cursor_x_ + width) / atlas_width_;
        float v1 = static_cast<float>(atlas_cursor_y_ + rows) / atlas_height_;
        uv = {u0, v0, u1, v1};

        atlas_cursor_x_ += width + 1;  // 1px padding
        atlas_row_height_ = std::max(atlas_row_height_, rows + 1);
        atlas_dirty_ = true;
        return true;
    }

    void upload_atlas() {
        if (!atlas_dirty_) return;
        if (!atlas_texture_obj_) {
            Renderer::TextureCreateInfo info{};
            info.width = atlas_width_;
            info.height = atlas_height_;
            info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
            info.mip_levels = 1;
            atlas_texture_obj_ = renderer_.create_texture(info);
        }

        // Expand alpha-only buffer to RGBA8 (white text with alpha from atlas)
        std::vector<uint8_t> rgba;
        rgba.reserve(atlas_pixels_.size() * 4);
        for (uint8_t a : atlas_pixels_) {
            rgba.push_back(255);  // R
            rgba.push_back(255);  // G
            rgba.push_back(255);  // B
            rgba.push_back(a);    // A
        }

        Renderer::TextureCreateInfo info{};
        info.width = atlas_width_;
        info.height = atlas_height_;
        info.format = Renderer::TextureCreateInfo::Format::R8G8B8A8_UNORM;
        info.initial_data = rgba.data();
        info.data_size = rgba.size();
        info.mip_levels = 1;

        atlas_texture_obj_ = renderer_.create_texture(info);
        atlas_dirty_ = false;
    }

    const GlyphInfo* get_glyph_ptr(uint32_t codepoint) const {
        auto it = glyphs_.find(codepoint);
        if (it != glyphs_.end()) return &it->second;
        return nullptr;
    }

    const GlyphInfo* ensure_glyph(uint32_t codepoint) {
        auto it = glyphs_.find(codepoint);
        if (it != glyphs_.end()) return &it->second;

        // Try to render on demand (this may insert into the map)
        if (ft_face_ && FT_Load_Char(ft_face_, codepoint, FT_LOAD_RENDER) == 0) {
            FT_Bitmap& bm = ft_face_->glyph->bitmap;
            if (bm.width > 0 && bm.rows > 0) {
                glm::vec4 uv;
                if (pack_glyph(bm, bm.width, bm.rows, uv)) {
                    GlyphInfo info;
                    info.codepoint = codepoint;
                    info.size = {static_cast<float>(bm.width), static_cast<float>(bm.rows)};
                    info.bearing = {static_cast<float>(ft_face_->glyph->bitmap_left),
                                    static_cast<float>(ft_face_->glyph->bitmap_top)};
                    info.advance = ft_face_->glyph->advance.x;
                    info.uv_rect = uv;
                    glyphs_[codepoint] = info;
                    upload_atlas();
                    // Re-fetch after potential rehash
                    return &glyphs_.at(codepoint);
                }
            } else {
                GlyphInfo info;
                info.codepoint = codepoint;
                info.size = {0, 0};
                info.bearing = {0, 0};
                info.advance = ft_face_->glyph->advance.x;
                info.uv_rect = {0, 0, 0, 0};
                glyphs_[codepoint] = info;
                return &glyphs_.at(codepoint);
            }
        }
        return nullptr;
    }

    /// Layout text into lines, computing per-glyph positions (pre-alignment).
    std::vector<LayoutLine> layout_text(const std::vector<uint32_t>& codepoints,
                                         float scale, float line_height,
                                         float wrap_width, float letter_spacing) {
        std::vector<LayoutLine> lines;
        lines.emplace_back();

        float cursor_x = 0.0f;
        float cursor_y = 0.0f;

        size_t i = 0;
        while (i < codepoints.size()) {
            uint32_t cp = codepoints[i];

            if (cp == '\n') {
                lines.back().width = cursor_x;
                lines.emplace_back();
                cursor_x = 0;
                cursor_y += line_height;
                ++i;
                continue;
            }

            // Collect a word (sequence of non-space, non-newline codepoints)
            size_t word_start = i;
            while (i < codepoints.size() && codepoints[i] != ' ' && codepoints[i] != '\n') {
                ++i;
            }

            // Measure the word
            float word_w = 0;
            for (size_t j = word_start; j < i; ++j) {
                if (auto* g = get_glyph_ptr(codepoints[j])) {
                    word_w += static_cast<float>(g->advance) / 64.0f * scale;
                    word_w += letter_spacing * scale;
                } else {
                    word_w += 0.5f * scale;  // fallback
                }
            }

            // Wrap if needed
            if (wrap_width > 0 && cursor_x + word_w > wrap_width && cursor_x > 0) {
                lines.back().width = cursor_x;
                lines.emplace_back();
                cursor_x = 0;
                cursor_y += line_height;
            }

            // Emit each glyph in the word
            for (size_t j = word_start; j < i; ++j) {
                uint32_t cp = codepoints[j];
                const GlyphInfo* g = get_glyph_ptr(cp);
                if (!g) {
                    ensure_glyph(cp);
                    g = get_glyph_ptr(cp);
                }

                LayoutGlyph lg;
                lg.codepoint = cp;
                lg.x = cursor_x;
                lg.y = cursor_y;
                if (g) {
                    lg.advance = static_cast<float>(g->advance) / 64.0f * scale;
                    lg.bearing_x = g->bearing.x * scale;
                    lg.bearing_y = g->bearing.y * scale;
                    lg.width = g->size.x * scale;
                    lg.height = g->size.y * scale;
                    lg.uv_rect = g->uv_rect;
                } else {
                    lg.advance = 0.5f * scale;
                    lg.width = 0;
                    lg.height = 0;
                    lg.uv_rect = {0, 0, 0, 0};
                }
                lg.advance += letter_spacing * scale;

                lines.back().glyphs.push_back(lg);
                cursor_x += lg.advance;
            }

            // Handle space between words
            if (i < codepoints.size() && codepoints[i] == ' ') {
                if (wrap_width > 0 && cursor_x + letter_spacing * scale > wrap_width) {
                    lines.back().width = cursor_x;
                    lines.emplace_back();
                    cursor_x = 0;
                    cursor_y += line_height;
                } else {
                    cursor_x += letter_spacing * scale;
                }
                ++i;
            }
        }

        if (!lines.empty()) {
            lines.back().width = cursor_x;
        }
        return lines;
    }

    void draw(const Renderer::TextRenderer::TextParams& params) {
        if (!ft_face_ || !atlas_texture_obj_) return;

        auto codepoints = decode_utf8(params.text);
        if (codepoints.empty()) return;

        float scale = params.size / font_size_;
        float line_height = params.line_height * font_size_ * scale;

        auto lines = layout_text(codepoints, scale, line_height,
                                 params.wrap_width, params.letter_spacing);

        sprite_renderer_->begin(glm::ortho(0.0f, static_cast<float>(renderer_.get_width()),
                                           static_cast<float>(renderer_.get_height()), 0.0f));

        for (auto& line : lines) {
            // Compute horizontal alignment offset
            float x_offset = 0.0f;
            if (params.wrap_width > 0) {
                switch (params.align) {
                    case Renderer::TextRenderer::TextParams::Align::Center:
                        x_offset = (params.wrap_width - line.width) * 0.5f;
                        break;
                    case Renderer::TextRenderer::TextParams::Align::Right:
                        x_offset = params.wrap_width - line.width;
                        break;
                    case Renderer::TextRenderer::TextParams::Align::Left:
                    default:
                        x_offset = 0.0f;
                        break;
                }
            }

            float base_y = params.position.y;

            for (auto& lg : line.glyphs) {
                const GlyphInfo* g = get_glyph_ptr(lg.codepoint);
                if (!g) continue;

                float gw = lg.width;
                float gh = lg.height;
                if (gw <= 0 || gh <= 0) continue;

                Renderer::SpriteRenderer::Sprite sprite;
                sprite.position = {
                    params.position.x + x_offset + lg.x + lg.bearing_x,
                    base_y + lg.y + (font_size_ - g->bearing.y) * scale - gh
                };
                sprite.size = {gw, gh};
                sprite.color = params.color;
                sprite.texture = atlas_texture_obj_;
                sprite.uv_rect = lg.uv_rect;
                sprite.layer = 0;
                sprite_renderer_->draw(sprite);
            }
        }

        sprite_renderer_->end();
    }

    glm::vec2 measure_text(const std::string& text, float size) const {
        if (!ft_face_) return {0, 0};

        auto codepoints = decode_utf8(text);
        float scale = size / font_size_;
        float line_height = 1.2f * font_size_ * scale;
        float cursor_x = 0;
        float cursor_y = 0;
        float max_width = 0;

        for (uint32_t cp : codepoints) {
            if (cp == '\n') {
                max_width = std::max(max_width, cursor_x);
                cursor_x = 0;
                cursor_y += line_height;
                continue;
            }
            if (cp == ' ') {
                cursor_x += 0.5f * size * scale;  // rough space width
                continue;
            }
            const GlyphInfo* g = get_glyph_ptr(cp);
            if (g) {
                cursor_x += static_cast<float>(g->advance) / 64.0f * scale;
            } else {
                cursor_x += 0.5f * size * scale;
            }
        }
        max_width = std::max(max_width, cursor_x);
        return {max_width, cursor_y + line_height};
    }
};

Renderer::TextRenderer::TextRenderer(Renderer& renderer)
    : impl_(std::make_unique<Impl>(renderer)) {}

Renderer::TextRenderer::~TextRenderer() = default;

bool Renderer::TextRenderer::load_font(const std::filesystem::path& path, float size) {
    return impl_->load_font(path, size);
}

void Renderer::TextRenderer::draw(const TextParams& params) {
    impl_->draw(params);
}

glm::vec2 Renderer::TextRenderer::measure_text(const std::string& text, float size) const {
    return impl_->measure_text(text, size);
}

} // namespace tensyoki
/**
 * renderer.cpp - OpenGL 4.5 renderer implementation (minimal viable implementation)
 */

#include "renderer.hpp"
#include "core/logging.hpp"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>

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

// === TextRenderer Implementation (Minimal) ===
struct Renderer::TextRenderer::Impl {
    Renderer& renderer_;
    explicit Impl(Renderer& r) : renderer_(r) {}
};

Renderer::TextRenderer::TextRenderer(Renderer& renderer)
    : impl_(std::make_unique<Impl>(renderer)) {}

Renderer::TextRenderer::~TextRenderer() = default;

bool Renderer::TextRenderer::load_font(const std::filesystem::path&, float) {
    // TODO: implement font loading (stb_truetype or freetype)
    return false;
}

void Renderer::TextRenderer::draw(const TextParams&) {
    // TODO: implement
}

glm::vec2 Renderer::TextRenderer::measure_text(const std::string&, float) const {
    return {0, 0};
}

} // namespace tensyoki

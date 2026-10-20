/**
 * renderer.hpp - Renderer abstraction layer
 * Currently only the OpenGL backend is implemented (Vulkan is a placeholder)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Missing OpenGL 4.5 Core Profile compressed texture format constants
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif

namespace tensyoki {

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    Hull,
    Domain
};

class Renderer {
public:
    enum class Backend {
        OpenGL,
        Vulkan,
        Auto
    };

    struct InitParams {
        Backend backend = Backend::Auto;
        void* window_handle = nullptr;  // SDL_Window*
        int width = 1280;
        int height = 720;
        bool vsync = true;
        int msaa_samples = 0;
        bool debug_overlay = false;
        bool srgb_framebuffer = true;
    };

    struct Viewport {
        int x = 0, y = 0;
        int width = 0, height = 0;
        float min_depth = 0.0f, max_depth = 1.0f;
    };

    struct DrawCall {
        uint32_t vertex_count = 0;
        uint32_t instance_count = 1;
        uint32_t first_vertex = 0;
        uint32_t first_instance = 0;
    };

    struct TextureCreateInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        enum class Format {
            R8G8B8A8_UNORM,
            R8G8B8A8_SRGB,
            R32G32B32A32_SFLOAT,
            R16G16B16A16_SFLOAT,
            D32_SFLOAT,
            D24_UNORM_S8_UINT,
            BC1_RGBA_UNORM,
            BC3_UNORM,
            BC7_UNORM
        } format = Format::R8G8B8A8_UNORM;
        enum class Usage {
            ShaderRead,
            RenderTarget,
            DepthStencil,
            TransferSrc,
            TransferDst
        } usage = Usage::ShaderRead;
        const void* initial_data = nullptr;
        size_t data_size = 0;
    };

    struct BufferCreateInfo {
        size_t size = 0;
        enum class Usage {
            VertexBuffer,
            IndexBuffer,
            UniformBuffer,
            StorageBuffer,
            IndirectBuffer,
            Staging
        } usage = Usage::VertexBuffer;
        const void* initial_data = nullptr;
    };

    struct PipelineCreateInfo {
        std::string vertex_shader;    // SPIR-V path or source
        std::string fragment_shader;
        struct VertexLayout {
            struct Attribute {
                std::string name;
                enum class Format { Float, Float2, Float3, Float4, Int, Int2, Int3, Int4 } format = Format::Float;
                uint32_t offset = 0;
            };
            std::vector<Attribute> attributes;
            uint32_t stride = 0;
        } vertex_layout;

        enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList, PointList } topology = PrimitiveTopology::TriangleList;
        enum class PolygonMode { Fill, Line, Point } polygon_mode = PolygonMode::Fill;
        enum class CullMode { None, Front, Back, FrontAndBack } cull_mode = CullMode::Back;
        bool depth_test = true;
        bool depth_write = true;
        enum class CompareOp { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always } depth_compare = CompareOp::Less;

        struct BlendState {
            bool enable = false;
            enum class Factor { Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha } src_color = Factor::SrcAlpha, dst_color = Factor::OneMinusSrcAlpha, src_alpha = Factor::One, dst_alpha = Factor::Zero;
            enum class Op { Add, Subtract, ReverseSubtract, Min, Max } op_color = Op::Add, op_alpha = Op::Add;
        } blend;
    };

    // === Resource types (nested classes) ===
    class Texture {
    public:
        Texture() = default;
        ~Texture() { if (id_) glDeleteTextures(1, &id_); }
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept : id_(other.id_), target_(other.target_) { other.id_ = 0; }
        Texture& operator=(Texture&& other) noexcept {
            if (this != &other) {
                if (id_) glDeleteTextures(1, &id_);
                id_ = other.id_; target_ = other.target_;
                other.id_ = 0;
            }
            return *this;
        }

        bool create(const TextureCreateInfo& info) {
            using Format = TextureCreateInfo::Format;

            target_ = (info.depth > 1) ? GL_TEXTURE_3D : (info.array_layers > 1) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
            glGenTextures(1, &id_);
            glBindTexture(target_, id_);

            GLenum internal_format = 0, format = 0, type = 0;
            switch (info.format) {
                case Format::R8G8B8A8_UNORM: internal_format = GL_RGBA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
                case Format::R8G8B8A8_SRGB: internal_format = GL_SRGB8_ALPHA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
                case Format::R32G32B32A32_SFLOAT: internal_format = GL_RGBA32F; format = GL_RGBA; type = GL_FLOAT; break;
                case Format::R16G16B16A16_SFLOAT: internal_format = GL_RGBA16F; format = GL_RGBA; type = GL_HALF_FLOAT; break;
                case Format::D32_SFLOAT: internal_format = GL_DEPTH_COMPONENT32F; format = GL_DEPTH_COMPONENT; type = GL_FLOAT; break;
                case Format::D24_UNORM_S8_UINT: internal_format = GL_DEPTH24_STENCIL8; format = GL_DEPTH_STENCIL; type = GL_UNSIGNED_INT_24_8; break;
                case Format::BC1_RGBA_UNORM: internal_format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
                case Format::BC3_UNORM: internal_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
                case Format::BC7_UNORM: internal_format = GL_COMPRESSED_RGBA_BPTC_UNORM; break;
            }

            const bool compressed = (info.format == Format::BC1_RGBA_UNORM ||
                                     info.format == Format::BC3_UNORM ||
                                     info.format == Format::BC7_UNORM);
            if (compressed) {
                if (info.initial_data && info.data_size > 0) {
                    glCompressedTexImage2D(target_, 0, internal_format,
                                           static_cast<GLsizei>(info.width), static_cast<GLsizei>(info.height), 0,
                                           static_cast<GLsizei>(info.data_size), info.initial_data);
                }
            } else {
                const void* pixels = (info.initial_data && info.data_size > 0) ? info.initial_data : nullptr;
                glTexImage2D(target_, 0, static_cast<GLint>(internal_format),
                             static_cast<GLsizei>(info.width), static_cast<GLsizei>(info.height), 0,
                             format, type, pixels);
            }

            if (info.mip_levels > 1) {
                glGenerateMipmap(target_);
            }

            glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, info.mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(target_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (target_ == GL_TEXTURE_3D || target_ == GL_TEXTURE_2D_ARRAY) {
                glTexParameteri(target_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            }

            glBindTexture(target_, 0);
            return id_ != 0;
        }

        GLuint get_id() const { return id_; }
        GLenum get_target() const { return target_; }

    private:
        GLuint id_ = 0;
        GLenum target_ = GL_TEXTURE_2D;
    };

    class Buffer {
    public:
        Buffer() = default;
        ~Buffer() { if (id_) glDeleteBuffers(1, &id_); }
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&& other) noexcept : id_(other.id_), target_(other.target_), size_(other.size_) { other.id_ = 0; }
        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                if (id_) glDeleteBuffers(1, &id_);
                id_ = other.id_; target_ = other.target_; size_ = other.size_;
                other.id_ = 0;
            }
            return *this;
        }

        bool create(const BufferCreateInfo& info) {
            switch (info.usage) {
                case BufferCreateInfo::Usage::VertexBuffer: target_ = GL_ARRAY_BUFFER; break;
                case BufferCreateInfo::Usage::IndexBuffer: target_ = GL_ELEMENT_ARRAY_BUFFER; break;
                case BufferCreateInfo::Usage::UniformBuffer: target_ = GL_UNIFORM_BUFFER; break;
                case BufferCreateInfo::Usage::StorageBuffer: target_ = GL_SHADER_STORAGE_BUFFER; break;
                case BufferCreateInfo::Usage::IndirectBuffer: target_ = GL_DRAW_INDIRECT_BUFFER; break;
                case BufferCreateInfo::Usage::Staging: target_ = GL_ARRAY_BUFFER; break;
            }
            glGenBuffers(1, &id_);
            glBindBuffer(target_, id_);
            glBufferData(target_, static_cast<GLsizeiptr>(info.size), info.initial_data, GL_DYNAMIC_DRAW);
            size_ = info.size;
            glBindBuffer(target_, 0);
            return id_ != 0;
        }

        void update(const void* data, size_t size, size_t offset = 0) {
            glBindBuffer(target_, id_);
            glBufferSubData(target_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
            glBindBuffer(target_, 0);
        }

        GLuint get_id() const { return id_; }
        GLenum get_target() const { return target_; }
        size_t get_size() const { return size_; }

    private:
        GLuint id_ = 0;
        GLenum target_ = GL_ARRAY_BUFFER;
        size_t size_ = 0;
    };

    class Pipeline {
    public:
        Pipeline() = default;
        ~Pipeline() = default;

        bool create(const PipelineCreateInfo& info) {
            // Minimal implementation: stores config only; shader compilation is handled by Shader
            info_ = info;
            return true;
        }

        const PipelineCreateInfo& get_info() const { return info_; }

    private:
        PipelineCreateInfo info_;
    };

    class Shader {
    public:
        Shader() = default;
        ~Shader() { if (id_) glDeleteProgram(id_); }
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&& other) noexcept : id_(other.id_) { other.id_ = 0; }
        Shader& operator=(Shader&& other) noexcept {
            if (this != &other) {
                if (id_) glDeleteProgram(id_);
                id_ = other.id_;
                other.id_ = 0;
            }
            return *this;
        }

        bool create(GLuint program_id) {
            id_ = program_id;
            return id_ != 0;
        }

        GLuint get_id() const { return id_; }

    private:
        GLuint id_ = 0;
    };

    // 2D sprite renderer
    class SpriteRenderer {
    public:
        struct Sprite {
            glm::vec2 position = {0, 0};
            glm::vec2 size = {1, 1};
            glm::vec4 color = {1, 1, 1, 1};
            float rotation = 0;
            glm::vec2 origin = {0.5f, 0.5f};  // Rotation origin
            std::shared_ptr<Texture> texture;
            glm::vec4 uv_rect = {0, 0, 1, 1};  // UV clipping
            int layer = 0;  // Render layer
        };

        explicit SpriteRenderer(Renderer& renderer);
        ~SpriteRenderer();

        SpriteRenderer(const SpriteRenderer&) = delete;
        SpriteRenderer& operator=(const SpriteRenderer&) = delete;

        void begin(const glm::mat4& projection);
        void draw(const Sprite& sprite);
        void draw_batch(const std::vector<Sprite>& sprites);
        void end();
        void flush();

        // Set the default texture (white pixel)
        void set_default_texture(std::shared_ptr<Texture> tex);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // Text renderer
    class TextRenderer {
    public:
        struct TextParams {
            std::string text;
            glm::vec2 position = {0, 0};
            float size = 24.0f;
            glm::vec4 color = {1, 1, 1, 1};
            float wrap_width = 0;  // 0 = no wrapping
            enum class Align { Left, Center, Right } align = Align::Left;
            float line_height = 1.2f;
            float letter_spacing = 0;
        };

        explicit TextRenderer(Renderer& renderer);
        ~TextRenderer();

        TextRenderer(const TextRenderer&) = delete;
        TextRenderer& operator=(const TextRenderer&) = delete;

        bool load_font(const std::filesystem::path& font_path, float size = 24.0f);
        void draw(const TextParams& params);
        glm::vec2 measure_text(const std::string& text, float size) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // === Renderer's own interface (declared only once) ===
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize / shut down
    bool init(const InitParams& params);
    void shutdown();

    // Resize
    void on_resize(int width, int height);

    // Begin/end frame
    void begin_frame();
    void end_frame();

    // Viewport/clipping
    void set_viewport(const Viewport& vp);
    void set_scissor(int x, int y, int width, int height);

    // Clear screen
    void clear(float r, float g, float b, float a, float depth = 1.0f, uint32_t stencil = 0);

    // Resource creation
    std::shared_ptr<Texture> create_texture(const TextureCreateInfo& info);
    std::shared_ptr<Buffer> create_buffer(const BufferCreateInfo& info);
    std::shared_ptr<Pipeline> create_pipeline(const PipelineCreateInfo& info);
    std::shared_ptr<Shader> create_shader(ShaderStage stage, const std::vector<uint32_t>& spirv);

    // Bind resources
    void bind_pipeline(std::shared_ptr<Pipeline> pipeline);
    void bind_vertex_buffer(uint32_t slot, std::shared_ptr<Buffer> buffer, uint32_t offset = 0);
    void bind_index_buffer(std::shared_ptr<Buffer> buffer, uint32_t offset = 0);
    void bind_texture(uint32_t slot, std::shared_ptr<Texture> texture);
    void bind_uniform_buffer(uint32_t slot, std::shared_ptr<Buffer> buffer, uint32_t offset = 0, uint32_t size = 0);

    // Draw
    void draw(const DrawCall& call);
    void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0);
    void draw_indirect(std::shared_ptr<Buffer> buffer, uint32_t offset = 0, uint32_t draw_count = 1, uint32_t stride = 0);

    // Compute shader
    void dispatch_compute(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

    // Get backend info
    Backend get_backend() const;
    void* get_device() const;  // VkDevice, or none (OpenGL)
    std::string get_backend_name() const;

    // Debug
    void push_debug_group(const std::string& name);
    void pop_debug_group();
    void insert_debug_marker(const std::string& marker);

    // Screenshot
    bool capture_screenshot(const std::filesystem::path& path);

    // Statistics
    struct Stats {
        uint64_t draw_calls = 0;
        uint64_t triangles = 0;
        uint64_t vertices = 0;
        uint64_t texture_binds = 0;
        uint64_t pipeline_binds = 0;
        float gpu_time_ms = 0.0f;
    };
    Stats get_stats() const;
    void reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tensyoki

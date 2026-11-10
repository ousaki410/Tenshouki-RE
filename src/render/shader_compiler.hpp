/**
 * shader_compiler.hpp - HLSL to SPIR-V/GLSL compiler interface
 * Currently a minimal implementation with no external dependencies (GLSL passthrough + SPIR-V validation); real HLSL/GLSL -> SPIR-V
 * compilation requires integrating a DXC or glslang backend.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <variant>
#include <unordered_map>

namespace tensyoki {

class ShaderCompiler {
public:
    enum class SourceLanguage {
        HLSL,
        GLSL,
        SPIRV
    };

    enum class TargetLanguage {
        SPIRV,
        GLSL,
        MSL,      // Metal (macOS/iOS)
        WGSL      // WebGPU
    };

    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessControl,
        TessEvaluation,
        RayGen,
        RayMiss,
        RayClosestHit,
        RayAnyHit,
        RayIntersection,
        Callable
    };

    struct CompileOptions {
        ShaderStage stage = ShaderStage::Fragment;
        std::string entry_point = "main";
        SourceLanguage source_lang = SourceLanguage::HLSL;
        TargetLanguage target_lang = TargetLanguage::SPIRV;

        // HLSL-specific
        std::string hlsl_shader_model = "5_0";  // Target Shader Model
        bool hlsl_enable_16bit_types = false;
        std::vector<std::string> hlsl_defines;  // Predefined macros
        std::vector<std::filesystem::path> include_dirs;

        // Optimization
        bool optimize = true;
        bool debug_info = false;

        // Resource bindings
        int base_vertex_binding = 0;
        int base_sampler_binding = 0;
        int base_texture_binding = 0;
        int base_constant_binding = 0;
        int base_storage_binding = 0;
    };

    struct CompileResult {
        bool success = false;
        std::vector<uint32_t> spirv;           // SPIR-V binary
        std::string glsl;                       // GLSL source
        std::string msl;                        // MSL source
        std::string wgsl;                       // WGSL source
        std::string error_message;              // Error message
        std::vector<std::string> warnings;      // Warning messages

        // Reflection info
        struct ResourceBinding {
            std::string name;
            uint32_t set = 0;
            uint32_t binding = 0;
            uint32_t count = 1;
            enum class Type { UniformBuffer, StorageBuffer, Sampler, Texture, StorageImage } type = Type::UniformBuffer;
            ShaderStage stage = ShaderStage::Fragment;
        };
        std::vector<ResourceBinding> bindings;

        struct PushConstantRange {
            uint32_t offset = 0;
            uint32_t size = 0;
            ShaderStage stage = ShaderStage::Fragment;
        };
        std::vector<PushConstantRange> push_constants;

        struct VertexInputAttribute {
            std::string name;
            uint32_t location = 0;
            enum class Format { Float, Float2, Float3, Float4, Int, Int2, Int3, Int4, UInt, UInt2, UInt3, UInt4 } format = Format::Float;
            uint32_t offset = 0;
        };
        std::vector<VertexInputAttribute> vertex_attributes;
    };

    ShaderCompiler();
    ~ShaderCompiler();

    // Owns a unique_ptr<Impl>; move-only
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;
    ShaderCompiler(ShaderCompiler&&) noexcept;
    ShaderCompiler& operator=(ShaderCompiler&&) noexcept;

    // Compile a source string
    CompileResult compile(const std::string& source, const CompileOptions& options);

    // Compile a file
    CompileResult compile_file(const std::filesystem::path& file_path, const CompileOptions& options);

    // Batch-compile a directory
    bool compile_directory(const std::filesystem::path& src_dir,
                           const std::filesystem::path& dst_dir,
                           const CompileOptions& base_options);

    // Extract from res_shader.bin and compile
    bool extract_and_compile_shaders(const std::filesystem::path& bin_path,
                                     const std::filesystem::path& output_dir);

    /// Parse all shader blobs from a res_shader.bin container without compiling.
    /// Returns metadata for each non-empty entry (version, type, source path, etc.).
    struct D3D9ShaderInfo {
        uint32_t entry_index = 0;
        std::string profile;           // "ps_2_0", "vs_2_0", etc.
        bool is_pixel_shader = false;
        uint32_t size_bytes = 0;
        std::string source_path;       // From DBUG comment
        std::vector<uint8_t> bytecode; // Raw D3D9 bytecode
    };
    std::vector<D3D9ShaderInfo> parse_shader_container(const std::filesystem::path& bin_path);

    /// Generate a best-effort GLSL fragment shader from a parsed D3D9 ps_2_0 blob.
    /// Returns empty string if conversion is not possible.
    std::string generate_glsl_from_d3d9(const D3D9ShaderInfo& info);

    // Get the list of supported shader models
    static std::vector<std::string> get_supported_shader_models();

    // Validate SPIR-V
    static bool validate_spirv(const std::vector<uint32_t>& spirv, std::string* error_out = nullptr);

    // SPIR-V disassembly (for debugging)
    static std::string disassemble_spirv(const std::vector<uint32_t>& spirv);

private:
    struct Impl;  // defined in shader_compiler.cpp
    std::unique_ptr<Impl> impl_;

    // HLSL shader model version mapping, "5_0" -> 50, returns 0 for invalid input
    static int hlsl_shader_model_to_version(const std::string& model);
};

// Convenience functions
namespace shader_utils {

// Extract info from D3D9-compiled ps_2_0/vs_2_0 bytecode
struct LegacyShaderInfo {
    std::string profile;           // "ps_2_0" or "vs_2_0"
    std::vector<uint8_t> bytecode; // Raw bytecode
    std::string source_hint;       // Source file path hint (if any)
};

std::optional<LegacyShaderInfo> parse_legacy_shader_blob(const std::vector<uint8_t>& data);

// Convert ps_2.0 syntax to modern HLSL (currently passthrough; conversion not implemented)
std::string convert_ps20_to_modern_hlsl(const std::string& ps20_source);

} // namespace shader_utils

} // namespace tensyoki

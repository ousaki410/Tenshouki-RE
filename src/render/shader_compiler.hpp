/**
 * shader_compiler.hpp - HLSL 到 SPIR-V/GLSL 编译器接口
 * 当前为无外部依赖的最小实现 (GLSL 透传 + SPIR-V 校验); 真正的 HLSL/GLSL -> SPIR-V
 * 编译需要接入 DXC 或 glslang 后端。
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

        // HLSL 特定
        std::string hlsl_shader_model = "5_0";  // 目标 Shader Model
        bool hlsl_enable_16bit_types = false;
        std::vector<std::string> hlsl_defines;  // 预定义宏
        std::vector<std::filesystem::path> include_dirs;

        // 优化
        bool optimize = true;
        bool debug_info = false;

        // 资源绑定
        int base_vertex_binding = 0;
        int base_sampler_binding = 0;
        int base_texture_binding = 0;
        int base_constant_binding = 0;
        int base_storage_binding = 0;
    };

    struct CompileResult {
        bool success = false;
        std::vector<uint32_t> spirv;           // SPIR-V 二进制
        std::string glsl;                       // GLSL 源码
        std::string msl;                        // MSL 源码
        std::string wgsl;                       // WGSL 源码
        std::string error_message;              // 错误信息
        std::vector<std::string> warnings;      // 警告信息

        // 反射信息
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

    // 拥有 unique_ptr<Impl>, 仅支持移动
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;
    ShaderCompiler(ShaderCompiler&&) noexcept;
    ShaderCompiler& operator=(ShaderCompiler&&) noexcept;

    // 编译源码字符串
    CompileResult compile(const std::string& source, const CompileOptions& options);

    // 编译文件
    CompileResult compile_file(const std::filesystem::path& file_path, const CompileOptions& options);

    // 批量编译目录
    bool compile_directory(const std::filesystem::path& src_dir,
                           const std::filesystem::path& dst_dir,
                           const CompileOptions& base_options);

    // 从 res_shader.bin 提取并编译
    bool extract_and_compile_shaders(const std::filesystem::path& bin_path,
                                     const std::filesystem::path& output_dir);

    // 获取支持的着色器模型列表
    static std::vector<std::string> get_supported_shader_models();

    // 验证 SPIR-V
    static bool validate_spirv(const std::vector<uint32_t>& spirv, std::string* error_out = nullptr);

    // SPIR-V 反汇编 (用于调试)
    static std::string disassemble_spirv(const std::vector<uint32_t>& spirv);

private:
    struct Impl;  // 在 shader_compiler.cpp 中定义
    std::unique_ptr<Impl> impl_;

    // HLSL 着色器模型版本映射, "5_0" -> 50, 非法输入返回 0
    static int hlsl_shader_model_to_version(const std::string& model);
};

// 便利函数
namespace shader_utils {

// 从 D3D9 编译的 ps_2_0/vs_2_0 字节码提取信息
struct LegacyShaderInfo {
    std::string profile;           // "ps_2_0" 或 "vs_2_0"
    std::vector<uint8_t> bytecode; // 原始字节码
    std::string source_hint;       // 源文件路径提示 (如果有)
};

std::optional<LegacyShaderInfo> parse_legacy_shader_blob(const std::vector<uint8_t>& data);

// 将 ps_2.0 语法转换为现代 HLSL (当前为透传, 尚未实现转换)
std::string convert_ps20_to_modern_hlsl(const std::string& ps20_source);

} // namespace shader_utils

} // namespace tensyoki

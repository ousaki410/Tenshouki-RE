/**
 * shader_compiler.cpp - Shader compiler implementation (minimal, no external dependencies)
 *
 * Supported: GLSL->GLSL passthrough, SPIR-V header validation/summary, shader model parsing,
 *         raw extraction from res_shader.bin, D3D9 shader version token parsing.
 * Not implemented: HLSL/GLSL -> SPIR-V (requires DXC or glslang), reflection, MSL/WGSL conversion.
 */

#include "shader_compiler.hpp"
#include "core/logging.hpp"
#include "resource/bin_parser.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace tensyoki {

struct ShaderCompiler::Impl {
    // Reserved for backend state (e.g. DXC/glslang instances)
};

ShaderCompiler::ShaderCompiler() : impl_(std::make_unique<Impl>()) {
    LOG_WARN("No HLSL/SPIR-V backend linked; ShaderCompiler runs in passthrough mode");
}

ShaderCompiler::~ShaderCompiler() = default;

ShaderCompiler::ShaderCompiler(ShaderCompiler&&) noexcept = default;
ShaderCompiler& ShaderCompiler::operator=(ShaderCompiler&&) noexcept = default;

ShaderCompiler::CompileResult ShaderCompiler::compile(const std::string& source,
                                                      const CompileOptions& options) {
    CompileResult result;

    if (source.empty()) {
        result.error_message = "empty shader source";
        return result;
    }

    if (options.source_lang == SourceLanguage::GLSL && options.target_lang == TargetLanguage::GLSL) {
        result.glsl = source;
        result.success = true;
        return result;
    }

    result.error_message = "no shader compiler backend available (DXC/glslang not linked); "
                           "only GLSL->GLSL passthrough is supported";
    LOG_ERROR("{}", result.error_message);
    return result;
}

ShaderCompiler::CompileResult ShaderCompiler::compile_file(const std::filesystem::path& file_path,
                                                           const CompileOptions& options) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        CompileResult result;
        result.error_message = "failed to open shader file: " + file_path.string();
        LOG_ERROR("{}", result.error_message);
        return result;
    }

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return compile(source, options);
}

bool ShaderCompiler::compile_directory(const std::filesystem::path& src_dir,
                                       const std::filesystem::path& dst_dir,
                                       const CompileOptions& base_options) {
    std::error_code ec;
    std::filesystem::create_directories(dst_dir, ec);
    if (ec) {
        LOG_ERROR("Failed to create output directory {}: {}", dst_dir.string(), ec.message());
        return false;
    }

    bool all_ok = true;
    size_t compiled = 0;
    std::filesystem::directory_iterator it(src_dir, ec);
    if (ec) {
        LOG_ERROR("Failed to read directory {}: {}", src_dir.string(), ec.message());
        return false;
    }

    for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            LOG_ERROR("Directory iteration failed: {}", ec.message());
            return false;
        }
        if (!it->is_regular_file(ec)) continue;

        auto result = compile_file(it->path(), base_options);
        if (!result.success) {
            all_ok = false;
            continue;
        }

        auto out_path = dst_dir / (it->path().stem().string() + ".glsl");
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            LOG_ERROR("Failed to write {}", out_path.string());
            all_ok = false;
            continue;
        }
        out.write(result.glsl.data(), static_cast<std::streamsize>(result.glsl.size()));
        ++compiled;
    }

    LOG_INFO("compile_directory: {} files written to {}", compiled, dst_dir.string());
    return all_ok;
}

bool ShaderCompiler::extract_and_compile_shaders(const std::filesystem::path& bin_path,
                                                 const std::filesystem::path& output_dir) {
    BinArchive archive;
    auto load_result = archive.load_from_file(bin_path);
    if (!load_result) {
        LOG_ERROR("Failed to load shader archive {}: {}", bin_path.string(), to_string(load_result.error()));
        return false;
    }

    // Currently only extracts the raw block without compiling (content format and compile backend need confirmation)
    LOG_WARN("extract_and_compile_shaders: compilation backend unavailable, extracting raw blobs only");
    auto extract_result = archive.extract_all(output_dir);
    return static_cast<bool>(extract_result);
}

std::vector<std::string> ShaderCompiler::get_supported_shader_models() {
    return {"5_0", "5_1", "6_0", "6_1", "6_2", "6_3", "6_4", "6_5", "6_6"};
}

bool ShaderCompiler::validate_spirv(const std::vector<uint32_t>& spirv, std::string* error_out) {
    constexpr uint32_t kSpirvMagic = 0x07230203u;
    constexpr size_t kHeaderWords = 5;

    auto fail = [&](const char* message) {
        if (error_out) *error_out = message;
        return false;
    };

    if (spirv.size() < kHeaderWords) return fail("SPIR-V too small (less than 5 header words)");
    if (spirv[0] != kSpirvMagic) return fail("invalid SPIR-V magic number");
    if (spirv[3] == 0) return fail("SPIR-V id bound is zero");
    return true;
}

std::string ShaderCompiler::disassemble_spirv(const std::vector<uint32_t>& spirv) {
    std::string error;
    if (!validate_spirv(spirv, &error)) {
        return "; invalid SPIR-V: " + error + "\n";
    }

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "; SPIR-V\n; Version: %u.%u\n; Generator: 0x%08X\n; Bound: %u\n; Words: %zu\n",
                  (spirv[1] >> 16) & 0xFFu, (spirv[1] >> 8) & 0xFFu,
                  spirv[2], spirv[3], spirv.size());
    return buffer;
}

int ShaderCompiler::hlsl_shader_model_to_version(const std::string& model) {
    unsigned major = 0, minor = 0;
    char tail = 0;
    if (std::sscanf(model.c_str(), "%u_%u%c", &major, &minor, &tail) != 2) return 0;
    if (major == 0 || major > 9 || minor > 9) return 0;
    return static_cast<int>(major * 10 + minor);
}

namespace shader_utils {

std::optional<LegacyShaderInfo> parse_legacy_shader_blob(const std::vector<uint8_t>& data) {
    if (data.size() < 4 || (data.size() % 4) != 0) return std::nullopt;

    // The first DWORD of a D3D9 shader is the version token: high 16 bits 0xFFFF=pixel, 0xFFFE=vertex;
    // low 16 bits = (major << 8) | minor
    const uint32_t token = static_cast<uint32_t>(data[0]) |
                           (static_cast<uint32_t>(data[1]) << 8) |
                           (static_cast<uint32_t>(data[2]) << 16) |
                           (static_cast<uint32_t>(data[3]) << 24);
    const uint32_t type = token >> 16;
    const uint32_t major = (token >> 8) & 0xFFu;
    const uint32_t minor = token & 0xFFu;

    if ((type != 0xFFFFu && type != 0xFFFEu) || major < 1 || major > 3) return std::nullopt;

    LegacyShaderInfo info;
    info.profile = std::string(type == 0xFFFFu ? "ps_" : "vs_") + std::to_string(major) + "_" + std::to_string(minor);
    info.bytecode = data;
    return info;
}

std::string convert_ps20_to_modern_hlsl(const std::string& ps20_source) {
    // TODO: implement ps_2_0 -> modern HLSL syntax conversion; currently passed through unchanged
    return ps20_source;
}

} // namespace shader_utils

} // namespace tensyoki

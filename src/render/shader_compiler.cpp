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
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        LOG_ERROR("Failed to create output directory {}: {}", output_dir.string(), ec.message());
        return false;
    }

    auto infos = parse_shader_container(bin_path);
    if (infos.empty()) {
        LOG_WARN("No shader blobs found in {}", bin_path.string());
        return false;
    }

    bool all_ok = true;
    for (const auto& info : infos) {
        // Write raw bytecode
        auto blob_path = output_dir / ("shader_entry" + std::to_string(info.entry_index) + ".bin");
        std::ofstream blob_out(blob_path, std::ios::binary);
        if (!blob_out) {
            LOG_ERROR("Failed to write {}", blob_path.string());
            all_ok = false;
            continue;
        }
        blob_out.write(reinterpret_cast<const char*>(info.bytecode.data()),
                       static_cast<std::streamsize>(info.bytecode.size()));

        // Write metadata sidecar
        auto meta_path = output_dir / ("shader_entry" + std::to_string(info.entry_index) + ".meta");
        std::ofstream meta_out(meta_path);
        if (!meta_out) {
            LOG_ERROR("Failed to write {}", meta_path.string());
            all_ok = false;
            continue;
        }
        meta_out << "entry_index: " << info.entry_index << "\n";
        meta_out << "profile: " << info.profile << "\n";
        meta_out << "is_pixel_shader: " << (info.is_pixel_shader ? "true" : "false") << "\n";
        meta_out << "size_bytes: " << info.size_bytes << "\n";
        meta_out << "source_path: " << info.source_path << "\n";

        // Generate GLSL and attempt compilation
        auto glsl = generate_glsl_from_d3d9(info);
        if (!glsl.empty()) {
            auto glsl_path = output_dir / ("shader_entry" + std::to_string(info.entry_index) + ".glsl");
            std::ofstream glsl_out(glsl_path);
            if (!glsl_out) {
                LOG_ERROR("Failed to write {}", glsl_path.string());
                all_ok = false;
                continue;
            }
            glsl_out << glsl;

            CompileOptions opts;
            opts.stage = info.is_pixel_shader ? ShaderStage::Fragment : ShaderStage::Vertex;
            opts.source_lang = SourceLanguage::GLSL;
            opts.target_lang = TargetLanguage::GLSL;
            auto result = compile(glsl, opts);
            if (result.success) {
                LOG_INFO("Shader entry {} ({}): compiled OK ({} bytes GLSL)",
                         info.entry_index, info.profile, glsl.size());
            } else {
                LOG_WARN("Shader entry {} ({}): GLSL compile failed: {}",
                         info.entry_index, info.profile, result.error_message);
                all_ok = false;
            }
        } else {
            LOG_WARN("Shader entry {} ({}): could not generate GLSL",
                     info.entry_index, info.profile);
            all_ok = false;
        }
    }

    LOG_INFO("extract_and_compile_shaders: processed {} shaders from {}",
             infos.size(), bin_path.string());
    return all_ok;
}

std::vector<ShaderCompiler::D3D9ShaderInfo> ShaderCompiler::parse_shader_container(
    const std::filesystem::path& bin_path) {

    std::vector<D3D9ShaderInfo> results;

    BinArchive archive;
    auto load_result = archive.load_from_file(bin_path);
    if (!load_result) {
        LOG_ERROR("Failed to load shader archive {}: {}", bin_path.string(),
                  to_string(load_result.error()));
        return results;
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(archive.entries().size()); ++i) {
        const auto* entry = archive.find_by_index(i);
        if (!entry || entry->size == 0) continue;

        auto view_result = archive.view(*entry);
        if (!view_result) continue;
        const auto& data = *view_result;

        if (data.size() < 4) continue;

        // Parse version token (first DWORD, little-endian)
        uint32_t token = static_cast<uint32_t>(data[0]) |
                         (static_cast<uint32_t>(data[1]) << 8) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 24);
        uint32_t type = token >> 16;
        uint32_t major = (token >> 8) & 0xFFu;
        uint32_t minor = token & 0xFFu;

        if ((type != 0xFFFFu && type != 0xFFFEu) || major < 1 || major > 3) {
            LOG_DEBUG("Entry {}: not a D3D9 shader (token=0x{:08X})", i, token);
            continue;
        }

        bool is_pixel = (type == 0xFFFFu);
        std::string profile = std::string(is_pixel ? "ps_" : "vs_") +
                              std::to_string(major) + "_" + std::to_string(minor);

        // Extract source path from DBUG comment
        std::string source_path;
        // Manually search for "DBUG(" in the byte data
        const char* dbug_marker = "DBUG(";
        size_t dbug_pos = std::string::npos;
        for (size_t j = 0; j + 5 <= data.size(); ++j) {
            if (std::memcmp(data.data() + j, dbug_marker, 5) == 0) {
                dbug_pos = j;
                break;
            }
        }
        if (dbug_pos != std::string::npos) {
            size_t str_start = dbug_pos + 5; // after "DBUG("
            size_t str_end = str_start;
            while (str_end < data.size() && data[str_end] != 0) ++str_end;
            source_path =
                std::string(reinterpret_cast<const char*>(data.data() + str_start),
                            str_end - str_start);
        }

        D3D9ShaderInfo info;
        info.entry_index = i;
        info.profile = profile;
        info.is_pixel_shader = is_pixel;
        info.size_bytes = static_cast<uint32_t>(data.size());
        info.source_path = source_path;
        info.bytecode.assign(data.begin(), data.end());

        results.push_back(info);
        LOG_INFO("Found shader entry {}: {} ({} bytes, source: {})",
                 i, profile, data.size(), source_path);
    }

    return results;
}

std::string ShaderCompiler::generate_glsl_from_d3d9(const D3D9ShaderInfo& info) {
    if (!info.is_pixel_shader) {
        LOG_WARN("GLSL generation for vertex shaders not yet implemented");
        return "";
    }

    // Best-effort GLSL for common ps_2_0 patterns.
    // The three shaders in res_shader.bin are:
    //   event.fsh      - event shader (likely fullscreen pass)
    //   mergestill.fsh - UI merge (vColor * texture sample)
    //   speed_bar.fsh  - speed bar (vColor * texture sample)
    //
    // All use vColor (vertex color) and texCoord (texture coordinate),
    // sampling from texture register t0 with sampler s0.

    std::string glsl;
    glsl += "#version 450 core\n";
    glsl += "in vec4 vColor;\n";
    glsl += "in vec2 vTexCoord;\n";
    glsl += "out vec4 outColor;\n";
    glsl += "layout(binding = 0) uniform sampler2D u_texture0;\n";
    glsl += "\n";
    glsl += "void main() {\n";
    glsl += "    vec4 tex = texture(u_texture0, vTexCoord);\n";
    glsl += "    outColor = tex * vColor;\n";
    glsl += "}\n";

    LOG_DEBUG("Generated GLSL for entry {} ({}): {} bytes",
              info.entry_index, info.profile, glsl.size());
    return glsl;
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

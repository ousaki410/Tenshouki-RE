/**
 * grp_parser.hpp - Image resource parser for files inside Koei .bin containers
 * 
 * Goal: parse subfiles in res_grp.bin, strip headers/palettes, and output standard RGBA8888 pixel arrays
 * for direct texture creation with SDL3/OpenGL
 */

#pragma once

#include "bin_parser.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <optional>
#include <expected>
#include <memory>

namespace tensyoki {

// Forward declaration
class GrpParser;

/// Parsed image data
struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;           // Internal format identifier
    std::vector<uint8_t> pixels;   // RGBA8888 data (width * height * 4)
    std::vector<uint8_t> palette;  // Palette data (for indexed color)
    bool has_alpha = false;
    bool is_compressed = false;
    
    // Get row stride
    size_t stride() const noexcept { return width * 4; }
    // Get total pixel byte count
    size_t pixel_size() const noexcept { return pixels.size(); }
};

/// Image format types inside grp
enum class GrpImageFormat : uint32_t {
    Unknown = 0,
    RGBA8888 = 1,      // 32-bit direct color
    RGB565 = 2,        // 16-bit direct color
    Indexed4 = 3,      // 4-bit indexed color (16-color palette)
    Indexed8 = 4,      // 8-bit indexed color (256-color palette)
    DXT1 = 5,          // DXT1 compressed
    DXT3 = 6,          // DXT3 compressed
    DXT5 = 7,          // DXT5 compressed
    Custom = 0xFFFF    // Custom/unknown format
};

/// grp subfile header structure (inferred from common Koei formats)
#pragma pack(push, 1)
struct GrpFileHeader {
    char magic[4];              // e.g. "GRP ", "IMG ", "TEX ", etc.
    uint32_t version;           // Version
    uint32_t width;             // Width
    uint32_t height;            // Height
    uint32_t format;            // GrpImageFormat
    uint32_t flags;             // Flags (compression, mip levels, etc.)
    uint32_t mip_count;         // Mipmap count
    uint32_t palette_offset;    // Palette offset (relative to file header)
    uint32_t palette_size;      // Palette size
    uint32_t pixel_offset;      // Pixel data offset (relative to file header)
    uint32_t pixel_size;        // Pixel data size
    uint32_t reserved[4];
};
#pragma pack(pop)

/// Parse errors
enum class GrpParseError {
    InvalidHeader,
    UnsupportedFormat,
    CorruptedPalette,
    CorruptedPixels,
    DecompressionFailed,
    InvalidDimensions,
    BufferTooSmall
};

constexpr std::string_view to_string(GrpParseError err) noexcept {
    using E = GrpParseError;
    switch (err) {
        case E::InvalidHeader:       return "Invalid GRP header";
        case E::UnsupportedFormat:   return "Unsupported image format";
        case E::CorruptedPalette:    return "Corrupted palette data";
        case E::CorruptedPixels:     return "Corrupted pixel data";
        case E::DecompressionFailed: return "Decompression failed";
        case E::InvalidDimensions:   return "Invalid image dimensions";
        case E::BufferTooSmall:      return "Buffer too small";
    }
    return "Unknown GRP error";
}

/// Result type (uses the Result type from bin_parser)
#include "bin_parser.hpp"
template<typename T>
using GrpResult = Result<T, GrpParseError>;

/// GRP image parser
class GrpParser {
public:
    GrpParser() = default;
    ~GrpParser() = default;
    
    GrpParser(const GrpParser&) = delete;
    GrpParser& operator=(const GrpParser&) = delete;
    GrpParser(GrpParser&&) = default;
    GrpParser& operator=(GrpParser&&) = default;
    
    /// Parse a single image from a span extracted by bin_parser
    GrpResult<ImageData> parse(std::span<const uint8_t> data) const;
    
    /// Batch parsing: parse all entries in a BinArchive that look like images
    std::vector<std::pair<BinFileEntry, ImageData>> parse_all(const BinArchive& archive) const;
    
    /// Check whether the data is in a valid image format
    static bool is_valid_image(std::span<const uint8_t> data);
    
    /// Get the list of supported formats
    static std::vector<GrpImageFormat> supported_formats();

private:
    // Internal parsing methods
    GrpResult<ImageData> parse_rgba8888(std::span<const uint8_t> data, 
                                         const GrpFileHeader& header) const;
    GrpResult<ImageData> parse_rgb565(std::span<const uint8_t> data, 
                                       const GrpFileHeader& header) const;
    GrpResult<ImageData> parse_indexed4(std::span<const uint8_t> data, 
                                         const GrpFileHeader& header) const;
    GrpResult<ImageData> parse_indexed8(std::span<const uint8_t> data, 
                                         const GrpFileHeader& header) const;
    GrpResult<ImageData> parse_dxt(std::span<const uint8_t> data, 
                                    const GrpFileHeader& header,
                                    GrpImageFormat format) const;
    
    // Palette parsing
    GrpResult<std::vector<uint8_t>> parse_palette(std::span<const uint8_t> data,
                                                   const GrpFileHeader& header) const;
    
    // Pixel decompression/conversion
    void apply_palette(const std::vector<uint8_t>& palette,
                        std::span<const uint8_t> src_indices,
                        std::vector<uint8_t>& dst_rgba,
                        uint32_t width, uint32_t height,
                        bool is_4bpp) const;
    
    // Utilities
    static GrpImageFormat detect_format(std::span<const uint8_t> data);
    static uint32_t read_u32_le(std::span<const uint8_t> data, size_t offset);
    static uint16_t read_u16_le(std::span<const uint8_t> data, size_t offset);
};

/// Convenience function: parse an image directly from a BinArchive entry
GrpResult<ImageData> parse_image_from_entry(const BinArchive& archive, 
                                             const BinFileEntry& entry);

/// Convenience function: try to detect and parse an image from arbitrary data
GrpResult<ImageData> try_parse_image(std::span<const uint8_t> data);

} // namespace tensyoki
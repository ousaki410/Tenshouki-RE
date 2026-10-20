/**
 * grp_parser.cpp - Implementation of the image resource parser for files inside Koei .bin containers
 */

#include "grp_parser.hpp"
#include "logging.hpp"
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace tensyoki {

// Little-endian read helpers
uint32_t GrpParser::read_u32_le(std::span<const uint8_t> data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t GrpParser::read_u16_le(std::span<const uint8_t> data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

GrpImageFormat GrpParser::detect_format(std::span<const uint8_t> data) {
    if (data.size() < 4) return GrpImageFormat::Unknown;
    
    // Check common magic numbers
    std::string_view magic(reinterpret_cast<const char*>(data.data()), 4);
    if (magic == "GRP " || magic == "IMG " || magic == "TEX " || magic == "SPR ") {
        if (data.size() >= sizeof(GrpFileHeader)) {
            uint32_t fmt = read_u32_le(data, 16); // offset of the format field
            return static_cast<GrpImageFormat>(fmt);
        }
    }
    
    // Try to infer from file size and content
    // TODO: smarter detection
    return GrpImageFormat::Unknown;
}

bool GrpParser::is_valid_image(std::span<const uint8_t> data) {
    return detect_format(data) != GrpImageFormat::Unknown;
}

std::vector<GrpImageFormat> GrpParser::supported_formats() {
    return {
        GrpImageFormat::RGBA8888,
        GrpImageFormat::RGB565,
        GrpImageFormat::Indexed4,
        GrpImageFormat::Indexed8,
        GrpImageFormat::DXT1,
        GrpImageFormat::DXT3,
        GrpImageFormat::DXT5
    };
}

GrpResult<ImageData> GrpParser::parse(std::span<const uint8_t> data) const {
    if (data.size() < sizeof(GrpFileHeader)) {
        return GrpResult<ImageData>::failure(GrpParseError::InvalidHeader);
    }
    
    // Parse header
    const GrpFileHeader* header = reinterpret_cast<const GrpFileHeader*>(data.data());
    
    // Validate magic number
    std::string_view magic(header->magic, 4);
    if (magic != "GRP " && magic != "IMG " && magic != "TEX " && magic != "SPR ") {
        LOG_WARN("Unknown GRP magic: '{}', attempting parse anyway", magic);
    }
    
    // Basic validation
    if (header->width == 0 || header->height == 0 || 
        header->width > 8192 || header->height > 8192) {
        return GrpResult<ImageData>::failure(GrpParseError::InvalidDimensions);
    }
    
    GrpImageFormat format = static_cast<GrpImageFormat>(header->format);
    LOG_DEBUG("Parsing GRP: {}x{} fmt={} mips={}", 
              header->width, header->height, header->format, header->mip_count);
    
    // Dispatch parsing by format
    switch (static_cast<GrpImageFormat>(header->format)) {
        case GrpImageFormat::RGBA8888:
            return parse_rgba8888(data, *header);
        case GrpImageFormat::RGB565:
            return parse_rgb565(data, *header);
        case GrpImageFormat::Indexed4:
            return parse_indexed4(data, *header);
        case GrpImageFormat::Indexed8:
            return parse_indexed8(data, *header);
        case GrpImageFormat::DXT1:
        case GrpImageFormat::DXT3:
        case GrpImageFormat::DXT5:
            return parse_dxt(data, *header, static_cast<GrpImageFormat>(header->format));
        default:
            LOG_WARN("Unsupported format 0x{:X}, trying RGBA8888 fallback", header->format);
            return parse_rgba8888(data, *header);
    }
}

GrpResult<ImageData> GrpParser::parse_rgba8888(std::span<const uint8_t> data, 
                                                const GrpFileHeader& header) const {
    ImageData img;
    img.width = header.width;
    img.height = header.height;
    img.format = static_cast<uint32_t>(GrpImageFormat::RGBA8888);
    img.has_alpha = true;
    
    size_t expected_size = header.width * header.height * 4;
    size_t pixel_offset = header.pixel_offset ? header.pixel_offset : sizeof(GrpFileHeader);
    
    if (data.size() < pixel_offset + expected_size) {
        LOG_ERROR("RGBA8888 data too small: need {}, have {}", 
                  pixel_offset + expected_size, data.size());
        return GrpResult<ImageData>::failure(GrpParseError::BufferTooSmall);
    }
    
    img.pixels.assign(data.begin() + pixel_offset, 
                      data.begin() + pixel_offset + expected_size);
    
    LOG_DEBUG("Parsed RGBA8888: {}x{} = {} bytes", 
              img.width, img.height, img.pixels.size());
    return img;
}

GrpResult<ImageData> GrpParser::parse_rgb565(std::span<const uint8_t> data, 
                                              const GrpFileHeader& header) const {
    ImageData img;
    img.width = header.width;
    img.height = header.height;
    img.format = static_cast<uint32_t>(GrpImageFormat::RGB565);
    img.has_alpha = false;
    
    size_t expected_size = header.width * header.height * 2;
    size_t pixel_offset = header.pixel_offset ? header.pixel_offset : sizeof(GrpFileHeader);
    
    if (data.size() < pixel_offset + expected_size) {
        return GrpResult<ImageData>::failure(GrpParseError::BufferTooSmall);
    }
    
    // Convert RGB565 to RGBA8888
    img.pixels.resize(header.width * header.height * 4);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(data.data() + pixel_offset);
    uint8_t* dst = img.pixels.data();
    
    for (size_t i = 0; i < header.width * header.height; ++i) {
        uint16_t pixel = read_u16_le(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(src + i), 2), 0);
        
        uint8_t r = (pixel >> 11) & 0x1F;
        uint8_t g = (pixel >> 5) & 0x3F;
        uint8_t b = pixel & 0x1F;
        
        // Expand to 8 bits
        *dst++ = (r << 3) | (r >> 2);
        *dst++ = (g << 2) | (g >> 4);
        *dst++ = (b << 3) | (b >> 2);
        *dst++ = 255; // Alpha
    }
    
    return img;
}

GrpResult<ImageData> GrpParser::parse_indexed8(std::span<const uint8_t> data, 
                                                const GrpFileHeader& header) const {
    ImageData img;
    img.width = header.width;
    img.height = header.height;
    img.format = static_cast<uint32_t>(GrpImageFormat::Indexed8);
    
    // Parse palette
    auto palette_result = parse_palette(data, header);
    if (!palette_result) return GrpResult<ImageData>::failure(palette_result.error());
    img.palette = std::move(*palette_result);
    
    size_t pixel_offset = header.pixel_offset ? header.pixel_offset : sizeof(GrpFileHeader);
    size_t expected_size = header.width * header.height;
    
    if (data.size() < pixel_offset + expected_size) {
        return GrpResult<ImageData>::failure(GrpParseError::BufferTooSmall);
    }
    
    img.pixels.resize(header.width * header.height * 4);
    apply_palette(img.palette, 
                  std::span(data.data() + pixel_offset, expected_size),
                  img.pixels, header.width, header.height, false);
    
    img.has_alpha = true; // Palette may contain transparency
    return img;
}

GrpResult<ImageData> GrpParser::parse_indexed4(std::span<const uint8_t> data, 
                                                const GrpFileHeader& header) const {
    ImageData img;
    img.width = header.width;
    img.height = header.height;
    img.format = static_cast<uint32_t>(GrpImageFormat::Indexed4);
    
    // Parse palette (16 colors)
    auto palette_result = parse_palette(data, header);
    if (!palette_result) return GrpResult<ImageData>::failure(palette_result.error());
    img.palette = std::move(*palette_result);
    
    size_t pixel_offset = header.pixel_offset ? header.pixel_offset : sizeof(GrpFileHeader);
    size_t expected_size = (header.width * header.height + 1) / 2; // 4bpp = 2 pixels/byte
    
    if (data.size() < pixel_offset + expected_size) {
        return GrpResult<ImageData>::failure(GrpParseError::BufferTooSmall);
    }
    
    img.pixels.resize(header.width * header.height * 4);
    apply_palette(img.palette,
                  std::span(data.data() + pixel_offset, expected_size),
                  img.pixels, header.width, header.height, true);
    
    img.has_alpha = true;
    return img;
}

GrpResult<ImageData> GrpParser::parse_dxt(std::span<const uint8_t> data, 
                                           const GrpFileHeader& header,
                                           GrpImageFormat format) const {
    // DXT decompression requires an external library (e.g. squish, stb_dxt) or a handwritten decoder
    // Return the raw data here for the caller to handle
    ImageData img;
    img.width = header.width;
    img.height = header.height;
    img.format = static_cast<uint32_t>(format);
    img.is_compressed = true;
    
    size_t pixel_offset = header.pixel_offset ? header.pixel_offset : sizeof(GrpFileHeader);
    size_t expected_size = header.pixel_size ? header.pixel_size : 
                           (header.width * header.height / 2); // DXT1 estimate
    
    if (data.size() < pixel_offset + expected_size) {
        return GrpResult<ImageData>::failure(GrpParseError::BufferTooSmall);
    }
    
    img.pixels.assign(data.begin() + pixel_offset, 
                      data.begin() + pixel_offset + expected_size);
    
    LOG_WARN("DXT format detected (0x{:X}), returning compressed data. Need DXT decoder.", 
             static_cast<uint32_t>(format));
    return img;
}

GrpResult<std::vector<uint8_t>> GrpParser::parse_palette(std::span<const uint8_t> data, 
                                                          const GrpFileHeader& header) const {
    std::vector<uint8_t> palette;
    
    if (header.palette_offset == 0 || header.palette_size == 0) {
        // No palette, or use the default palette
        return GrpResult<std::vector<uint8_t>>::failure(GrpParseError::CorruptedPalette);
    }
    
    if (data.size() < header.palette_offset + header.palette_size) {
        return GrpResult<std::vector<uint8_t>>::failure(GrpParseError::CorruptedPalette);
    }
    
    size_t entry_count = header.palette_size / 4; // assume 4 bytes per RGBA entry
    palette.resize(entry_count * 4);
    
    const uint8_t* src = data.data() + header.palette_offset;
    std::memcpy(palette.data(), src, header.palette_size);
    
    LOG_DEBUG("Parsed palette: {} entries ({} bytes)", entry_count, palette.size());
    return palette;
}

void GrpParser::apply_palette(const std::vector<uint8_t>& palette,
                               std::span<const uint8_t> src_indices,
                               std::vector<uint8_t>& dst_rgba,
                               uint32_t width, uint32_t height,
                               bool is_4bpp) const {
    uint8_t* dst = dst_rgba.data();
    size_t palette_entries = palette.size() / 4;
    
    if (is_4bpp) {
        // 4bpp: 2 pixels per byte (high nibble + low nibble)
        for (size_t i = 0; i < src_indices.size(); ++i) {
            uint8_t byte = src_indices[i];
            uint8_t idx1 = (byte >> 4) & 0x0F;
            uint8_t idx2 = byte & 0x0F;
            
            for (uint8_t idx : {idx1, idx2}) {
                if (dst >= dst_rgba.data() + dst_rgba.size()) break;
                size_t pal_idx = std::min<size_t>(idx, palette_entries - 1) * 4;
                *dst++ = palette[pal_idx];     // R
                *dst++ = palette[pal_idx + 1]; // G
                *dst++ = palette[pal_idx + 2]; // B
                *dst++ = palette[pal_idx + 3]; // A
            }
        }
    } else {
        // 8bpp: 1 pixel per byte
        for (size_t i = 0; i < src_indices.size(); ++i) {
            uint8_t idx = src_indices[i];
            size_t pal_idx = std::min<size_t>(idx, palette_entries - 1) * 4;
            *dst++ = palette[pal_idx];     // R
            *dst++ = palette[pal_idx + 1]; // G
            *dst++ = palette[pal_idx + 2]; // B
            *dst++ = palette[pal_idx + 3]; // A
        }
    }
}

std::vector<std::pair<BinFileEntry, ImageData>> GrpParser::parse_all(const BinArchive& archive) const {
    std::vector<std::pair<BinFileEntry, ImageData>> results;
    
    for (const auto& entry : archive.entries()) {
        // Simple heuristic: filter likely images by extension or type
        const char* ext = entry.guess_extension().c_str();
        if (strcmp(ext, ".tex") != 0 && strcmp(ext, ".spr") != 0 && 
            strcmp(ext, ".img") != 0 && entry.type != 1) {
            continue; // skip obviously non-image entries
        }
        
        auto view_result = archive.view(entry);
        if (!view_result) continue;
        
        auto img_result = this->parse(*view_result);
        if (img_result) {
            results.emplace_back(entry, std::move(*img_result));
        } else {
            LOG_DEBUG("Failed to parse '{}' as image: {}", 
                      entry.name, to_string(img_result.error()));
        }
    }
    
    LOG_INFO("Parsed {} images from {} entries", results.size(), archive.entries().size());
    return results;
}

GrpResult<ImageData> parse_image_from_entry(const BinArchive& archive, 
                                             const BinFileEntry& entry) {
    auto view_result = archive.view(entry);
    if (!view_result) {
        return GrpResult<ImageData>::failure(GrpParseError::InvalidHeader);
    }
    
    GrpParser parser;
    return parser.parse(*view_result);
}

GrpResult<ImageData> try_parse_image(std::span<const uint8_t> data) {
    GrpParser parser;
    return parser.parse(data);
}

} // namespace tensyoki
/**
 * gt1g_decoder.hpp - GT1G 0600 texture block decoder
 *
 * Decodes a GT1G texture block (as found in nested LINK data entries of
 * res_grp.bin, res_exp.bin, res_exp_pc.bin) into per-texture pixel data.
 *
 * Supported pixel formats (observed in the game data):
 *   fmt=1  BGRA8888  - 4 bytes per pixel, alpha last, row-major
 *   fmt=6  DXT1 / BC1 - 8 bytes per 4x4 block
 *   fmt=8  DXT5 / BC3 - 16 bytes per 4x4 block
 *
 * Decoded output is always RGBA8888 so it can be uploaded to OpenGL as
 * GL_RGBA8 with no format-specific shader logic.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <optional>

namespace tensyoki {

/// Pixel format of a decoded texture
enum class GT1GFormat {
    Unknown = 0,
    BGRA8888 = 1,   // 4 bpp, uncompressed
    DXT1 = 6,       // BC1 compressed, 8 bytes per 4x4 block
    DXT5 = 8,       // BC3 compressed, 16 bytes per 4x4 block
};

/// One decoded texture inside a GT1G block
struct GT1GTexture {
    uint32_t index = 0;            // texture index inside the GT1G block
    uint32_t width = 0;            // texture width in pixels
    uint32_t height = 0;           // texture height in pixels
    uint32_t format_id = 0;        // raw format byte from the header
    GT1GFormat format = GT1GFormat::Unknown;
    uint8_t mip_info = 0;          // raw mip_info byte
    bool is_compressed = false;    // true for DXT1/DXT5
    std::vector<uint8_t> pixels;   // RGBA8888 decoded pixel data (width*height*4)
    std::vector<uint8_t> compressed; // raw compressed bytes (for direct BC upload)
    size_t data_offset = 0;        // pixel data offset relative to GT1G block start
    size_t data_size = 0;          // pixel data size in bytes
};

/// Result of decoding a GT1G block
struct GT1GDecoderResult {
    uint32_t texture_count = 0;
    std::vector<GT1GTexture> textures;
    std::string error;
};

/**
 * Decode a GT1G 0600 block into per-texture RGBA8888 pixel data.
 *
 * `block` should point at the start of the GT1G data (the "GT1G0600" magic).
 * Returns a result with decoded textures; on failure `error` is set and
 * `textures` may be partially populated.
 */
GT1GDecoderResult decode_gt1g_block(std::span<const uint8_t> block);

} // namespace tensyoki

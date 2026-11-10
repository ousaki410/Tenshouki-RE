/**
 * gt1g_decoder.cpp - GT1G 0600 texture block decoder
 */

#include "gt1g_decoder.hpp"
#include "core/logging.hpp"
#include <algorithm>
#include <cstring>
#include <numeric>

namespace tensyoki {

namespace {

// ---------------------------------------------------------------------------
// Little-endian helpers
// ---------------------------------------------------------------------------

uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// RGB565 -> RGB888
// ---------------------------------------------------------------------------

void rgb565_to_rgb888(uint16_t packed, uint8_t& r, uint8_t& g, uint8_t& b) {
    const uint8_t r5 = static_cast<uint8_t>((packed >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((packed >> 5)  & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>( packed        & 0x1F);
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));   // expand 5->8 bits
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));   // expand 6->8 bits
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));   // expand 5->8 bits
}

// ---------------------------------------------------------------------------
// DXT1 block decoder (8 bytes per 4x4 block)
// ---------------------------------------------------------------------------

void decode_dxt1_block(const uint8_t* block, uint8_t* out) {
    const uint16_t c0 = read_u16_le(block);
    const uint16_t c1 = read_u16_le(block + 2);
    const uint32_t indices = read_u32_le(block + 4);

    uint8_t r0, g0, b0, r1, g1, b1;
    rgb565_to_rgb888(c0, r0, g0, b0);
    rgb565_to_rgb888(c1, r1, g1, b1);

    // Build the 4-color palette
    uint8_t cr[4], cg[4], cb[4], ca[4];
    cr[0] = r0; cg[0] = g0; cb[0] = b0; ca[0] = 255;
    cr[1] = r1; cg[1] = g1; cb[1] = b1; ca[1] = 255;

    if (c0 > c1) {
        cr[2] = static_cast<uint8_t>((2 * r0 + r1) / 3);
        cg[2] = static_cast<uint8_t>((2 * g0 + g1) / 3);
        cb[2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
        ca[2] = 255;
        cr[3] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
        cg[3] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
        cb[3] = static_cast<uint8_t>((b0 + 2 * b1) / 3);
        ca[3] = 255;
    } else {
        cr[2] = static_cast<uint8_t>((r0 + r1) / 2);
        cg[2] = static_cast<uint8_t>((g0 + g1) / 2);
        cb[2] = static_cast<uint8_t>((b0 + b1) / 2);
        ca[2] = 255;
        cr[3] = 0; cg[3] = 0; cb[3] = 0; ca[3] = 0;  // transparent
    }

    // Write 4x4 pixels (row-major)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int bit = 2 * (y * 4 + x);
            const uint32_t idx = (indices >> bit) & 0x03;
            size_t pixel = ((size_t)y * 4 + x) * 4;
            out[pixel + 0] = cr[idx];
            out[pixel + 1] = cg[idx];
            out[pixel + 2] = cb[idx];
            out[pixel + 3] = ca[idx];
        }
    }
}

// ---------------------------------------------------------------------------
// DXT5 block decoder (16 bytes per 4x4 block)
// ---------------------------------------------------------------------------

void decode_dxt5_block(const uint8_t* block, uint8_t* out) {
    const uint8_t a0 = block[0];
    const uint8_t a1 = block[1];

    // Read the 64-bit alpha index bitfield (bytes 2..7, little-endian)
    uint64_t alpha_bits = 0;
    for (int i = 0; i < 6; ++i) {
        alpha_bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }

    const uint16_t c0 = read_u16_le(block + 8);
    const uint16_t c1 = read_u16_le(block + 10);
    const uint32_t indices = read_u32_le(block + 12);

    uint8_t r0, g0, b0, r1, g1, b1;
    rgb565_to_rgb888(c0, r0, g0, b0);
    rgb565_to_rgb888(c1, r1, g1, b1);

    // Build the 4-color palette (same as DXT1)
    uint8_t cr[4], cg[4], cb[4];
    cr[0] = r0; cg[0] = g0; cb[0] = b0;
    cr[1] = r1; cg[1] = g1; cb[1] = b1;
    if (c0 > c1) {
        cr[2] = static_cast<uint8_t>((2 * r0 + r1) / 3);
        cg[2] = static_cast<uint8_t>((2 * g0 + g1) / 3);
        cb[2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
        cr[3] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
        cg[3] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
        cb[3] = static_cast<uint8_t>((b0 + 2 * b1) / 3);
    } else {
        cr[2] = static_cast<uint8_t>((r0 + r1) / 2);
        cg[2] = static_cast<uint8_t>((g0 + g1) / 2);
        cb[2] = static_cast<uint8_t>((b0 + b1) / 2);
        cr[3] = 0; cg[3] = 0; cb[3] = 0;
    }

    // Build the 8-alpha palette
    uint8_t ca[8];
    ca[0] = a0;
    ca[1] = a1;
    if (a0 > a1) {
        for (int i = 2; i < 8; ++i) {
            ca[i] = static_cast<uint8_t>(((8 - i) * a0 + (i - 1) * a1) / 7);
        }
    } else {
        for (int i = 2; i < 6; ++i) {
            ca[i] = static_cast<uint8_t>(((6 - i) * a0 + (i - 1) * a1) / 5);
        }
        ca[6] = 0;
        ca[7] = 255;
    }

    // Write 4x4 pixels (row-major)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int pixel_index = y * 4 + x;

            // Color index: 2 bits per pixel
            const int cbit = 2 * pixel_index;
            const uint32_t cidx = (indices >> cbit) & 0x03;

            // Alpha index: 3 bits per pixel, packed into 6 bytes (48 bits)
            const int abit = 3 * pixel_index;
            const uint32_t aidx = (alpha_bits >> abit) & 0x07;

            size_t pixel = static_cast<size_t>(pixel_index) * 4;
            out[pixel + 0] = cr[cidx];
            out[pixel + 1] = cg[cidx];
            out[pixel + 2] = cb[cidx];
            out[pixel + 3] = ca[aidx];
        }
    }
}

} // namespace

// ===========================================================================
// GT1G block decoder
// ===========================================================================

GT1GDecoderResult decode_gt1g_block(std::span<const uint8_t> block) {
    GT1GDecoderResult result;

    if (block.size() < 0x1C || block[0] != 'G' || block[1] != 'T' ||
        block[2] != '1' || block[3] != 'G') {
        result.error = "not a GT1G block (bad magic)";
        return result;
    }
    if (block.size() < 8 || std::memcmp(block.data() + 4, "0600", 4) != 0) {
        result.error = "unsupported GT1G version tag";
        return result;
    }

    const uint32_t total_size = read_u32_le(block.data() + 0x08);
    const uint32_t header_size = read_u32_le(block.data() + 0x0C);
    const uint32_t texture_count = read_u32_le(block.data() + 0x10);

    if (texture_count == 0 || texture_count > 4096) {
        result.error = "invalid GT1G texture count: " + std::to_string(texture_count);
        return result;
    }
    if (header_size != 0x1C + 4 * texture_count) {
        LOG_WARN("GT1G header_size 0x{:X} != expected 0x{:X}",
                 header_size, 0x1C + 4 * texture_count);
    }

    const uint32_t effective_size = total_size ? total_size : static_cast<uint32_t>(block.size());
    if (effective_size > block.size()) {
        result.error = "GT1G total_size exceeds block size";
        return result;
    }

    result.texture_count = texture_count;
    result.textures.reserve(texture_count);

    for (uint32_t i = 0; i < texture_count; ++i) {
        const uint32_t rel = read_u32_le(block.data() + header_size + 4 * i);
        const size_t start = static_cast<size_t>(header_size) + rel;

        if (start + 20 > effective_size) {
            result.error = "texture " + std::to_string(i) + " header out of bounds";
            return result;
        }

        const uint8_t* thdr = block.data() + start;
        const uint8_t mip_info = thdr[0];
        const uint8_t format_id = thdr[1];
        const uint8_t size_log2 = thdr[2];

        const unsigned width_log2 = size_log2 >> 4;
        const unsigned height_log2 = size_log2 & 0x0F;
        const uint32_t width = 1u << width_log2;
        const uint32_t height = 1u << height_log2;

        // Per-texture extra header (observed: 0x0C bytes at +8)
        const size_t data_start = start + 20;

        // Determine the end of this texture's pixel data
        size_t data_end = effective_size;
        if (i + 1 < texture_count) {
            const uint32_t next_rel =
                read_u32_le(block.data() + header_size + 4 * (i + 1));
            const size_t next_start = static_cast<size_t>(header_size) + next_rel;
            if (next_start > data_start) {
                data_end = next_start;
            }
        }
        if (data_end < data_start) {
            data_end = data_start;
        }
        const size_t data_size = data_end - data_start;

        GT1GTexture tex;
        tex.index = i;
        tex.width = width;
        tex.height = height;
        tex.format_id = format_id;
        tex.mip_info = mip_info;
        tex.data_offset = data_start - header_size;  // relative to GT1G block start
        tex.data_size = data_size;

        // Classify format
        switch (format_id) {
            case 1:
                tex.is_compressed = false;
                break;
            case 6:
                tex.is_compressed = true;
                break;
            case 8:
                tex.is_compressed = true;
                break;
            default:
                tex.is_compressed = false;
                LOG_WARN("GT1G texture {}: unknown format_id {}", i, format_id);
                break;
        }

        // Decode pixels
        const uint8_t* pix = block.data() + data_start;
        const size_t pix_avail = (data_start + data_size <= effective_size)
            ? data_size
            : (effective_size - data_start);

        if (tex.is_compressed) {
            // Keep raw compressed data for possible direct BC upload
            tex.compressed.assign(pix, pix + pix_avail);

            // Also decode to RGBA8888 for immediate use
            const uint32_t block_w = (width + 3) / 4;
            const uint32_t block_h = (height + 3) / 4;
            tex.pixels.resize(width * height * 4, 0);

            for (uint32_t by = 0; by < block_h; ++by) {
                for (uint32_t bx = 0; bx < block_w; ++bx) {
                    const size_t block_offset = (by * block_w + bx) *
                        (format_id == 6 ? 8 : 16);
                    if (block_offset + (format_id == 6 ? 8 : 16) > pix_avail) break;
                    uint8_t* tile = tex.pixels.data() +
                        ((by * 4) * width + (bx * 4)) * 4;
                    const uint8_t* blk = pix + block_offset;
                    if (format_id == 6) {
                        // DXT1: 8 bytes per 4x4 block
                        const uint16_t c0 = read_u16_le(blk);
                        const uint16_t c1 = read_u16_le(blk + 2);
                        const uint32_t indices = read_u32_le(blk + 4);
                        uint8_t r0, g0, b0, r1, g1, b1;
                        rgb565_to_rgb888(c0, r0, g0, b0);
                        rgb565_to_rgb888(c1, r1, g1, b1);
                        uint8_t cr[4], cg[4], cb[4], ca[4];
                        cr[0] = r0; cg[0] = g0; cb[0] = b0; ca[0] = 255;
                        cr[1] = r1; cg[1] = g1; cb[1] = b1; ca[1] = 255;
                        if (c0 > c1) {
                            cr[2] = static_cast<uint8_t>((2 * r0 + r1) / 3);
                            cg[2] = static_cast<uint8_t>((2 * g0 + g1) / 3);
                            cb[2] = static_cast<uint8_t>((2 * b0 + b1) / 3); ca[2] = 255;
                            cr[3] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
                            cg[3] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
                            cb[3] = static_cast<uint8_t>((b0 + 2 * b1) / 3); ca[3] = 255;
                        } else {
                            cr[2] = static_cast<uint8_t>((r0 + r1) / 2);
                            cg[2] = static_cast<uint8_t>((g0 + g1) / 2);
                            cb[2] = static_cast<uint8_t>((b0 + b1) / 2); ca[2] = 255;
                            cr[3] = 0; cg[3] = 0; cb[3] = 0; ca[3] = 0;
                        }
                        for (int y = 0; y < 4; ++y) {
                            for (int x = 0; x < 4; ++x) {
                                const int bit = 2 * (y * 4 + x);
                                const uint32_t idx = (indices >> bit) & 0x03;
                                size_t pixel = ((size_t)y * 4 + x) * 4;
                                tile[pixel + 0] = cr[idx];
                                tile[pixel + 1] = cg[idx];
                                tile[pixel + 2] = cb[idx];
                                tile[pixel + 3] = ca[idx];
                            }
                        }
                    } else {
                        // DXT5: 16 bytes per 4x4 block
                        const uint8_t a0 = blk[0];
                        const uint8_t a1 = blk[1];
                        uint64_t alpha_bits = 0;
                        for (int i = 0; i < 6; ++i) {
                            alpha_bits |= static_cast<uint64_t>(blk[2 + i]) << (8 * i);
                        }
                        const uint16_t c0 = read_u16_le(blk + 8);
                        const uint16_t c1 = read_u16_le(blk + 10);
                        const uint32_t indices = read_u32_le(blk + 12);
                        uint8_t r0, g0, b0, r1, g1, b1;
                        rgb565_to_rgb888(c0, r0, g0, b0);
                        rgb565_to_rgb888(c1, r1, g1, b1);
                        uint8_t cr[4], cg[4], cb[4];
                        cr[0] = r0; cg[0] = g0; cb[0] = b0;
                        cr[1] = r1; cg[1] = g1; cb[1] = b1;
                        if (c0 > c1) {
                            cr[2] = static_cast<uint8_t>((2 * r0 + r1) / 3);
                            cg[2] = static_cast<uint8_t>((2 * g0 + g1) / 3);
                            cb[2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
                            cr[3] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
                            cg[3] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
                            cb[3] = static_cast<uint8_t>((b0 + 2 * b1) / 3);
                        } else {
                            cr[2] = static_cast<uint8_t>((r0 + r1) / 2);
                            cg[2] = static_cast<uint8_t>((g0 + g1) / 2);
                            cb[2] = static_cast<uint8_t>((b0 + b1) / 2);
                            cr[3] = 0; cg[3] = 0; cb[3] = 0;
                        }
                        uint8_t ca[8];
                        ca[0] = a0; ca[1] = a1;
                        if (a0 > a1) {
                            for (int i = 2; i < 8; ++i) {
                                ca[i] = static_cast<uint8_t>(((8 - i) * a0 + (i - 1) * a1) / 7);
                            }
                        } else {
                            for (int i = 2; i < 6; ++i) {
                                ca[i] = static_cast<uint8_t>(((6 - i) * a0 + (i - 1) * a1) / 5);
                            }
                            ca[6] = 0; ca[7] = 255;
                        }
                        for (int y = 0; y < 4; ++y) {
                            for (int x = 0; x < 4; ++x) {
                                const int pixel_index = y * 4 + x;
                                const int cbit = 2 * pixel_index;
                                const uint32_t cidx = (indices >> cbit) & 0x03;
                                const int abit = 3 * pixel_index;
                                const uint32_t aidx = (alpha_bits >> abit) & 0x07;
                                size_t pixel = static_cast<size_t>(pixel_index) * 4;
                                tile[pixel + 0] = cr[cidx];
                                tile[pixel + 1] = cg[cidx];
                                tile[pixel + 2] = cb[cidx];
                                tile[pixel + 3] = ca[aidx];
                            }
                        }
                    }
                }
            }
        } else {
            // BGRA8888: 4 bytes per pixel, convert to RGBA8888
            const size_t expected = static_cast<size_t>(width) * height * 4;
            tex.pixels.reserve(expected);
            for (size_t p = 0; p + 3 < pix_avail && p / 4 < expected; p += 4) {
                // BGRA -> RGBA
                tex.pixels.push_back(pix[p + 2]); // R
                tex.pixels.push_back(pix[p + 1]); // G
                tex.pixels.push_back(pix[p + 0]); // B
                tex.pixels.push_back(pix[p + 3]); // A
            }
            // Pad if short
            while (tex.pixels.size() < expected) {
                tex.pixels.push_back(0);
            }
            tex.pixels.resize(expected);
        }

        result.textures.push_back(std::move(tex));
    }

    LOG_INFO("GT1G block decoded: {} textures", result.texture_count);
    return result;
}

} // namespace tensyoki

# GT1G Texture Container

Status: draft
Last updated: 2026-10-12
Parser: none yet
Tests: none

## 1. Overview

`GT1G` with version string `0600` is the data section of nested `LINK` containers in `res/res_grp.bin`, `res/res_exp.bin`, and `res/res_exp_pc.bin` (see [link.md](link.md)). It holds one or more textures. The matching descriptor table in the nested `LINK` lists rectangles inside those textures.

The tag is commonly associated with a texture container format used by KOEI TECMO titles. The layout below is derived only from the samples; public documentation has not been compared yet.

## 2. Samples

| Evidence ID | Where | Size of GT1G block |
| --- | --- | ---: |
| E-0017 | res_grp.bin, top-level entry 0, file offset 4,728 | 0x480070 |
| E-0016 | res_exp.bin, top-level entry 0 | 0x40038 |
| E-0016 | res_exp_pc.bin, top-level entry 0 | 0x2054 |

Only the `res_grp.bin` header has been dumped. The other two sizes are used for arithmetic checks.

Provenance: third-party repackaged distribution; re-verify against retail files before `confirmed`.

## 3. Conventions

- Endianness: little-endian.
- Offsets in the texture offset table are relative to the end of the fixed header area (`header_size`), not to the start of the block.

## 4. Block Header

| Offset | Size | Type | Name | Value in res_grp entry 0 | Confidence |
| ---: | ---: | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | magic | `GT1G` | observed |
| 0x04 | 4 | char[4] | version | `0600` | observed |
| 0x08 | 4 | u32 | total_size | 0x480070, equal to the nested `LINK` data size | observed |
| 0x0C | 4 | u32 | header_size | 0x28, equal to 28 + 4 * count | observed |
| 0x10 | 4 | u32 | texture_count | 3 | observed |
| 0x14 | 4 | u32 | unknown | 0 | hypothesis (platform id) |
| 0x18 | 4 | u32 | unknown | 0 | hypothesis (extra data size) |
| 0x1C | 4 * count | u32[] | per_texture_value | 1, 1, 1 | hypothesis (flags) |

## 5. Texture Offset Table

Starts at `header_size` (0x28), with `texture_count` u32 values. For `res_grp` entry 0: 0x0C, 0x400020, 0x440034.

Offsets are relative to `header_size`. Check: texture 0 begins at 0x28 + 0x0C = 0x34 and the first pixel byte is observed at 0x48, so the texture entry header is 20 bytes (0x48 - 0x34 = 0x14). Both facts are observed.

## 6. Texture Entry Header (20 bytes)

Observed for texture 0 of `res_grp` entry 0 (bytes in file order):

```
10 01 aa 00 11 11 01 10 | 0c 00 00 00 | 00 00 00 00 | 00 00 00 01
```

| Offset | Size | Name | Value | Confidence |
| ---: | ---: | --- | --- | --- |
| 0x00 | 1 | mip_info | 0x10 (high nibble 1, possibly mip level count) | hypothesis |
| 0x01 | 1 | format | 0x01 | hypothesis (pixel format id) |
| 0x02 | 1 | size_log2 | 0xAA, both nibbles 10, which gives 1024 by 1024 | observed arithmetic |
| 0x03 to 0x07 | 5 | unknown | `00 11 11 01 10` | unknown |
| 0x08 | 4 | extra_header_size | 0x0C | hypothesis |
| 0x0C | 8 | extra_header | `00..00 00 00 00 01` | unknown |

The 12-byte tail (u32 0x0C plus 8 bytes) accounts for the difference between an 8-byte texture header and the observed 20 bytes.

## 7. Size Arithmetic

For `res_grp` entry 0, `total_size` 0x480070 decomposes exactly as:

| Part | Bytes |
| --- | ---: |
| Fixed header + per-texture values (0x28) | 40 |
| Offset table (3 * 4) | 12 |
| Three texture entry headers (3 * 20) | 60 |
| Pixel data of texture 0 (1024 * 1024 * 4) | 4,194,304 |
| Pixel data of texture 1 (span 0x40014 minus 20) | 262,144 |
| Pixel data of texture 2 (remaining) | 262,144 |
| Total | 4,718,704 (= 0x480070) |

The first term group is 0x70 = 112 bytes, which also matches `28 * (count + 1)` for the three samples: `res_exp_pc` (count 2, 0x54), `res_exp` (count 1, 0x38), `res_grp` (count 3, 0x70). Textures 1 and 2 are therefore 262,144 pixel bytes each, which would be 256 by 256 at 4 bytes per pixel if they use the same pixel format as texture 0. Their entry headers have not been read.

For `res_exp_pc` the two 32 by 32 records give 2 * 32 * 32 * 4 = 8,192 pixel bytes, matching 0x2054 - 0x54. For `res_exp` all 111 records are 20 by 20 and fit in 240 by 200, which is inside a 256 by 256 texture of 262,144 bytes.

## 8. Pixel Data

The first 12 pixels of texture 0 (rows start at file offset 0x12C0) have the form `b0 c0 c2 ff`, with a fourth byte of 0xFF and slowly varying neighbours. This is consistent with 4 bytes per pixel, alpha last, linear row-major order. The channel order (RGBA or BGRA) is not decided.

No pixel bytes are stored in this repository.

## 9. Hypotheses

- Format id 0x01 means 32 bits per pixel; the exact channel order needs a rendered comparison with a known sprite.
- Textures 1 and 2 of `res_grp` entry 0 are 256 by 256.
- `descriptor.last` (see link.md) is the index of the texture in this block. Evidence: `res_exp_pc` has 2 textures and last values 0 and 1; `res_grp` entry 0 has 3 textures and last values 0, 1, and 2.

## 10. Open Questions

- Which record reaches x + w = 1,280 in `res_grp` entry 0? A 1,280-wide record cannot fit in a 1,024-wide texture, so either it belongs to a different texture than assumed or coordinates are not always pixels.
- Meaning of bytes 0x03 to 0x07 and 0x0C to 0x13 in the texture entry header.
- Pixel formats of textures 1 and 2 and of other nested containers in `res_grp.bin` (entries 1 to 20).

## 11. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-10-12 | Initial draft | E-0016..E-0019 |

# LINK Container (res/*.bin)

Status: draft
Last updated: 2026-10-12
Parser: none yet (the existing BIN parser must be re-checked against this layout)
Tests: none

## 1. Overview

All six files under `res/` start with the ASCII magic `LINK`: `res_grp.bin`, `res_else.bin`, `res_exp.bin`, `res_exp_pc.bin`, `res_shader.bin`, and `res_snd.bin`. Each holds a table of (offset, size) pairs followed by the payloads. Some payloads are themselves `LINK` containers.

The names are read as "graphics", "other", "expression", "shader", and "sound" resources, which is an inference from names only.

## 2. Samples

| Evidence ID | File | Size (bytes) | SHA-256 |
| --- | --- | ---: | --- |
| E-0001 | res/res_grp.bin | 365,574,996 | e31700c0262cc6aa82f8b9e6a643fa049e88c2293caadf0f42da99b6bf3ff5d7 |
| E-0002 | res/res_else.bin | 164,540 | a939e8fff4912013907c5b09a1e718ea79c27786e7eb6ec6d69e9ad5eb707049 |
| E-0003 | res/res_exp.bin | 263,636 | b2a1558690c1be07d2052103f8a31b282c4cff68bdac785d2b826e3206e9b173 |
| E-0004 | res/res_exp_pc.bin | 8,400 | b776805223fc17ec201cab23f2339db03072ad80a8f86389b5461f6a2ef8cb1f |
| E-0007 | res/res_shader.bin | 6,284 | not recorded yet |
| E-0007 | res/res_snd.bin | 3,914,544 | not recorded yet |

Provenance: the samples come from a third-party repackaged distribution. Hashes must be re-taken from retail Steam files before any field is promoted to `confirmed`.

## 3. Conventions

- Endianness: little-endian (observed in all samples).
- Offsets in the top-level table are absolute from file start.
- Offsets in nested containers are relative to the start of the nested `LINK` (observed in three samples, see section 7).

## 4. Top-Level Header (16 bytes)

| Offset | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | magic | `LINK` | observed | E-0001..E-0007 |
| 0x04 | 4 | u32 | count | number of table entries | observed | E-0008 |
| 0x08 | 4 | u32 | version_or_flags | value 1 in all six files | hypothesis | E-0008 |
| 0x0C | 4 | u32 | reserved | value 0 in all six files | hypothesis | E-0008 |

Observed counts: `res_grp` 21, `res_else` 3, `res_exp` 1, `res_exp_pc` 1, `res_shader` 6, `res_snd` 2.

## 5. Top-Level Entry Table

Starts at 0x10, with `count` entries of 8 bytes each.

| Offset in entry | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | offset | absolute offset of the payload | observed | E-0008 |
| 0x04 | 4 | u32 | size | payload size in bytes (may be 0) | observed | E-0008 |

Example (`res_shader.bin`, E-0014): (0x40, 0xDAC), (0xDF0, 0x428), (0x121C, 0x66C), (0x188C, 0), (0x1890, 0), (0x1894, 0).

## 6. Invariants

Checked with `tools/link_check.py` over all six files (E-0008) and corrected in E-0015:

- [x] First entry offset equals `16 + 8 * count` in all six files.
- [x] Between consecutive entries, `next.offset = prev.offset + prev.size + 4` holds for every gap in every file (histogram `{4: n}` in all six).
- [x] In all six files, the last non-empty payload ends exactly 4 bytes before the end of the file. (`res_shader.bin`: entry 2 ends at 6,280 and the file size is 6,284.)
- [x] Empty entries (size 0) continue the offset progression, so trailing empty entries can have an offset at or beyond the end of the file. In `res_shader.bin`, entry 3 has offset 6,284 (equal to the file size) and entries 4 and 5 have offsets 6,288 and 6,292.
- [x] The 4 bytes after the first payload of `res_exp_pc.bin` (file offsets 8,396 to 8,399) are `ef cd ab 89`, which reads as the little-endian value 0x89ABCDEF (E-0013). Only one gap has been inspected.

## 7. Nested LINK Containers

First payloads of `res_grp`, `res_exp`, and `res_exp_pc` start with a nested `LINK`. Observed nested header words:

| File | +0x04 | +0x08 | +0x0C | +0x10 | Entry at +0x20 (offset, size) | offset + size |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| res_grp entry 0 | 1 | 0x20 | 0x174 (372) | 0x40 | (0x11C0, 0x480070) | 0x481230 = outer size |
| res_exp entry 0 | 1 | 0x20 | 0x6F (111) | 0x40 | (0x580, 0x40038) | 0x405B8 = outer size |
| res_exp_pc entry 0 | 1 | 0x20 | 2 | 0x40 | (0x60, 0x2054) | 0x20B4 = outer size |

Bytes +0x14 to +0x1F are zero in all three. Offsets are relative to the nested start.

### Nested header fields

| Offset | Type | Name | Meaning | Confidence |
| ---: | --- | --- | --- | --- |
| +0x04 | u32 | version | 1 | hypothesis |
| +0x08 | u32 | data_table_offset | 0x20, location of the data entry | hypothesis |
| +0x0C | u32 | record_count | number of descriptor records | observed (res_grp: records decode consistently, E-0011) |
| +0x10 | u32 | descriptor_offset | 0x40, location of the descriptor table | observed |
| +0x20 | u32 x2 | data_offset, data_size | one data entry; offset + size equals the outer payload size | observed |

### Descriptor records (12 bytes each)

Observed in `res_grp.bin` entry 0 (E-0011, E-0012). Decoded as `u16 x, u16 y, u16 w, u16 h, u32 last`:

| Record | x | y | w | h | last |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 528 | 452 | 33 | 33 | 0 |
| 1 | 92 | 620 | 10 | 33 | 0 |
| 2 | 102 | 620 | 13 | 33 | 0 |
| 3 | 115 | 620 | 10 | 33 | 0 |
| 4 | 812 | 604 | 10 | 33 | 0 |
| 363 | 64 | 688 | 48 | 32 | 0 |
| 364 | 480 | 720 | 30 | 30 | 0 |
| 365 | 148 | 704 | 42 | 26 | 0 |
| 366 | 948 | 748 | 20 | 20 | 0 |
| 367 | 968 | 748 | 20 | 20 | 0 |

Evidence that the first four u16 values are a rectangle: records 1 to 3 satisfy `x + w = next x` on the same row (92 + 10 = 102, 102 + 13 = 115), and records 366 and 367 satisfy 948 + 20 = 968. Heights are equal along a row. Records are therefore sprite or glyph rectangles in a texture. The meaning of the last u32 is unknown: it is 0 for all ten records seen here, while in `res_exp_pc.bin` the two 32 by 32 records have last = 0 and last = 1, which suggests a texture index (hypothesis).

Data start rule (fits all three samples): `data_offset = ((descriptor_offset + 12 * n) rounded down to 16) + 16` with n = 372, 111, and 2 giving 0x11C0, 0x580, and 0x60.

### Data section size hypothesis

| File | data_size | minus header guess | Pixel bytes | Reading |
| --- | ---: | ---: | ---: | --- |
| res_exp_pc | 0x2054 | 0x54 | 8,192 | two 32 by 32 images at 4 bytes per pixel |
| res_exp | 0x40038 | 0x38 | 262,144 | one 256 by 256 image at 4 bytes per pixel |
| res_grp | 0x480070 | 0x70 | 4,718,592 | 1,024 by 1,152 at 4 bytes per pixel, or several textures |

The header sizes 0x54, 0x38, and 0x70 equal 28 times 3, 2, and 4. This could mean a `GT1G` header of 28 bytes per texture plus 28 bytes fixed, giving 2, 1, and 3 textures. Largest rectangle extents seen in `res_grp` entry 0 are x + w = 988 and y + h = 768, which are inside a 1,024-wide texture. All of this is a hypothesis; the data start has not been dumped yet.

## 8. Other Payload Observations

| Payload | Observation | Confidence |
| --- | --- | --- |
| res_else entry 1 at 0x2C | starts with ASCII `LS11`; meaning unknown | observed |
| res_exp_pc nested data start | nested offset 0x60 lands at absolute 0x78, where ASCII `GT1G0600` appears | observed |
| res_shader entry 0 at 0x40 | begins with bytes `00 02 ff ff`, then `fe ff fb 01`, then ASCII `DBUG`; a path string beginning `C:\Project\` appears 0x30 bytes later | observed |

The `res_shader` bytes match the Direct3D 9 shader token layout: version token 0xFFFF0200 (pixel shader 2.0), a comment token, and a `DBUG` debug-info comment that embeds a source path. This matches the DirectX 9.0c requirement of the game. Treat as a hypothesis until confirmed against the public Direct3D 9 shader bytecode documentation. Shader bytes must not be committed.

## 9. Hypotheses

- `GT1G0600` is the magic of a texture container used by KOEI TECMO titles (commonly called G1T). Test: compare the structure after the magic with public community documentation, without copying code.
- `LS11` is a sub-container or compressed-stream tag. Test: capture a longer excerpt and look for repeating structure.
- The 4-byte gap after each payload is the constant 0x89ABCDEF used as a terminator or integrity marker. Test: read the gap after every entry in every file.
- The three entries of `res_shader.bin` hold compiled Direct3D 9 shaders. Test: parse the leading tokens of each payload.

## 10. Open Questions

- Is the 4-byte gap always 0x89ABCDEF?
- What is the last u32 of a descriptor record (texture index, flags, or padding)?
- What are the 16 bytes between descriptor end and data start?
- What is the exact structure of the data section header (28 bytes per texture)?
- Do the other 20 `res_grp.bin` entries use the same nested layout?

## 11. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-10-12 | Initial draft from header excerpts | E-0001..E-0004 |
| 2026-10-12 | Full validation of six files, nested layout hypothesis | E-0007..E-0010 |
| 2026-10-12 | Decoded descriptor records, gap sentinel, shader payload; corrected res_shader tail analysis | E-0011..E-0015 |

# LINK Container (res/*.bin)

Status: draft
Last updated: 2026-10-12
Parser: none yet (the existing BIN parser must be re-checked against this layout)
Tests: none

## 1. Overview

All six files under `res/` start with the ASCII magic `LINK`: `res_grp.bin`, `res_else.bin`, `res_exp.bin`, `res_exp_pc.bin`, `res_shader.bin`, and `res_snd.bin`. Each holds a table of (offset, size) pairs followed by the payloads. Some payloads are themselves `LINK` containers whose data section is a `GT1G` texture block (see [gt1g.md](gt1g.md)).

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

Checked with `tools/link_check.py` over all six files (E-0008, corrected by E-0015):

- [x] First entry offset equals `16 + 8 * count` in all six files.
- [x] Between consecutive entries, `next.offset = prev.offset + prev.size + 4` holds for every gap in every file (histogram `{4: n}` in all six).
- [x] In all six files, the last non-empty payload ends exactly 4 bytes before the end of the file.
- [x] Empty entries (size 0) continue the offset progression, so trailing empty entries can have an offset at or beyond the end of the file. In `res_shader.bin`, entry 3 has offset 6,284 (equal to the file size) and entries 4 and 5 have offsets 6,288 and 6,292.
- [x] The 4 bytes that follow a payload are `ef cd ab 89` (little-endian 0x89ABCDEF) in the two gaps inspected: after the only payload of `res_exp_pc.bin` (file offset 8,396) and after entry 0 of `res_grp.bin` (file offset 4,723,432) (E-0013, E-0018). The other gaps have not been read.

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
| +0x0C | u32 | record_count | number of descriptor records (372, 111, 2) | observed |
| +0x10 | u32 | descriptor_offset | 0x40, location of the descriptor table | observed |
| +0x20 | u32 x2 | data_offset, data_size | one data entry; offset + size equals the outer payload size | observed |

### Descriptor records (12 bytes each)

Decoded as `u16 x, u16 y, u16 w, u16 h, u32 last`. Observed in all 372 + 111 + 2 records through `tools/link_nested.py` (E-0016), with the first ten records of `res_grp` entry 0 dumped in E-0011 and E-0012:

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

Evidence that the first four u16 values are a rectangle: on the same row, `x + w` equals the next `x` (92 + 10 = 102, 102 + 13 = 115, 948 + 20 = 968).

Statistics from `link_nested.py` (E-0016):

| File | Records | `last` values | max x+w | max y+h | Sizes |
| --- | ---: | --- | ---: | ---: | --- |
| res_grp entry 0 | 372 | 0: 370, 1: 1, 2: 1 | 1280 | 775 | (12,12) x54, (3,32) x27, (53,26) x26, (18,18) x21 |
| res_exp entry 0 | 111 | 0: 111 | 240 | 200 | (20,20) x111 |
| res_exp_pc entry 0 | 2 | 0: 1, 1: 1 | 32 | 32 | (32,32) x2 |

`last` is the index of the texture inside the `GT1G` block: `res_exp_pc` has 2 textures with values 0 and 1, and `res_grp` entry 0 has 3 textures with values 0, 1, and 2 (see gt1g.md). `res_exp` has 111 records at 20 by 20 inside 240 by 200, which fits a grid of 12 by 10 cells.

Unresolved: `res_grp` entry 0 reaches x + w = 1280, but texture 0 is 1,024 wide (gt1g.md). The record that causes this has not been identified. Run `tools/link_nested.py` to list records beyond 1,024.

Data start rule: `data_offset = ((descriptor_offset + 12 * n) rounded down to 16) + 16`. It fits the three samples and predicted 0x11C0 for `res_grp`, which was then observed as the actual `GT1G` start (E-0017).

## 8. Other Payload Observations

| Payload | Observation | Confidence |
| --- | --- | --- |
| res_else entry 1 at 0x2C | starts with ASCII `LS11`; meaning unknown | observed |
| res_shader entry 0 at 0x40 | begins with bytes `00 02 ff ff`, then `fe ff fb 01`, then ASCII `DBUG`; a path string beginning `C:\Project\` appears 0x30 bytes later | observed |

The `res_shader` bytes match the Direct3D 9 shader token layout: version token 0xFFFF0200 (pixel shader 2.0), a comment token, and a `DBUG` debug-info comment that embeds a source path. This matches the DirectX 9.0c requirement of the game. Treat as a hypothesis until confirmed against the public Direct3D 9 shader bytecode documentation. Shader bytes must not be committed.

## 9. Hypotheses

- `LS11` is a sub-container or compressed-stream tag. Test: capture a longer excerpt and look for repeating structure.
- The 4-byte gap after each payload is the constant 0x89ABCDEF used as a terminator or integrity marker. Test: read the gap after every entry in every file.
- The three non-empty entries of `res_shader.bin` hold compiled Direct3D 9 shaders. Test: parse the leading tokens of each payload.

## 10. Open Questions

- Does every gap equal 0x89ABCDEF?
- Which `res_grp` entry-0 record reaches x + w = 1,280?
- What are the 16 bytes between descriptor end and data start?
- Do the other 20 `res_grp.bin` entries use the same nested layout?

## 11. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-10-12 | Initial draft from header excerpts | E-0001..E-0004 |
| 2026-10-12 | Full validation of six files, nested layout hypothesis | E-0007..E-0010 |
| 2026-10-12 | Decoded descriptor records, gap sentinel, shader payload; corrected res_shader tail analysis | E-0011..E-0015 |
| 2026-10-12 | Statistics for three nested containers, texture index evidence, second sentinel, GT1G split into gt1g.md | E-0016..E-0019 |

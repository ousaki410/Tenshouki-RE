# LINK Container (res/*.bin)

Status: draft
Last updated: 2026-10-12
Parser: none yet (the existing BIN parser must be re-checked against this layout)
Tests: none

## 1. Overview

Four files under `res/` start with the ASCII magic `LINK`: `res_grp.bin`, `res_else.bin`, `res_exp.bin`, and `res_exp_pc.bin`. Each holds a table of (offset, size) pairs followed by the payloads. `res_shader.bin` and `res_snd.bin` have not been inspected yet.

The file names are read as "graphics", "other", and "expression" resources, but this is an inference from names only.

## 2. Samples

| Evidence ID | File | Size (bytes) | SHA-256 |
| --- | --- | ---: | --- |
| E-0001 | res/res_grp.bin | 365,574,996 | e31700c0262cc6aa82f8b9e6a643fa049e88c2293caadf0f42da99b6bf3ff5d7 |
| E-0002 | res/res_else.bin | 164,540 | a939e8fff4912013907c5b09a1e718ea79c27786e7eb6ec6d69e9ad5eb707049 |
| E-0003 | res/res_exp.bin | 263,636 | b2a1558690c1be07d2052103f8a31b282c4cff68bdac785d2b826e3206e9b173 |
| E-0004 | res/res_exp_pc.bin | 8,400 | b776805223fc17ec201cab23f2339db03072ad80a8f86389b5461f6a2ef8cb1f |

Provenance: the samples come from a third-party repackaged distribution. Hashes must be re-taken from retail Steam files before any field is promoted to `confirmed`.

## 3. Conventions

- Endianness: little-endian (observed in all four samples).
- Offsets: absolute from file start for the top-level table.
- Alignment: payloads are followed by 4 bytes before the next payload (see invariants).

## 4. Top-Level Header (16 bytes)

| Offset | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | magic | `LINK` | observed | E-0001..E-0004 |
| 0x04 | 4 | u32 | count | number of table entries (21, 3, 1, 1) | observed | E-0001..E-0004 |
| 0x08 | 4 | u32 | version_or_flags | value 1 in all samples | hypothesis | E-0001..E-0004 |
| 0x0C | 4 | u32 | reserved | value 0 in all samples | hypothesis | E-0001..E-0004 |

## 5. Top-Level Entry Table

Starts at 0x10, with `count` entries of 8 bytes each.

| Offset in entry | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | offset | absolute offset of the payload | observed | E-0001..E-0004 |
| 0x04 | 4 | u32 | size | payload size in bytes (may be 0) | observed | E-0001..E-0004 |

## 6. Invariants (checked on the first bytes available)

- [x] First entry offset equals `16 + 8 * count` in all four samples (184, 40, 24, 24).
- [x] For consecutive entries, `next.offset = prev.offset + prev.size + 4` (4-byte gap) for all 13 gaps observable in `res_grp.bin` and both gaps in `res_else.bin`.
- [x] For `res_exp.bin`, `res_exp_pc.bin`, and `res_else.bin`, `last.offset + last.size + 4` equals the file size.
- [ ] For `res_grp.bin`, the same end-of-file check is not yet done, because only 128 bytes were captured. Run `tools/link_check.py` to verify.
- [ ] The meaning of the 4-byte gap (padding, checksum, or terminator) is unknown. Contents have not been captured.

A zero-size entry is valid: entry 0 of `res_else.bin` has offset 0x28 and size 0.

## 7. Payload Observations

| Payload | Observation | Confidence |
| --- | --- | --- |
| res_else entry 1 at 0x2C | starts with ASCII `LS11`; meaning unknown | observed |
| res_exp entry 0 at 0x18 | starts with a nested `LINK`; nested entry (offset 0x580, size 0x40038) satisfies offset + size = outer entry size (0x405B8) | observed |
| res_exp_pc entry 0 at 0x18 | starts with a nested `LINK`; nested entry (offset 0x60, size 0x2054) satisfies offset + size = outer entry size (0x20B4); nested offset 0x60 lands at absolute 0x78 where `GT1G0600` appears | observed |

The nested header is not identical to the top-level header. Observed nested words (res_exp_pc): `LINK`, 1, 0x20, 2, 0x40, 0, 0, 0, then at +0x20 the pair (0x60, 0x2054). Nested offsets appear to be relative to the nested start. The meanings of +0x08, +0x0C, and +0x10 are not known.

## 8. Hypotheses

- `GT1G0600` is the magic of a texture container used by KOEI TECMO titles (commonly called G1T). Test: compare the structure after the magic with public community documentation, without copying code.
- `res_exp.bin` payload contains many small fixed-size image descriptors (the words `0x14 0x14` repeat, which could be width and height 20 by 20). Test: dump the nested table and check whether sizes fit.
- `LS11` is a sub-container or compressed-stream tag. Test: capture a longer excerpt and look for a repeating structure.

## 9. Open Questions

- What is stored in the 4-byte gap between payloads?
- What are the nested header fields at +0x08, +0x0C, and +0x10?
- Do `res_shader.bin` and `res_snd.bin` use the same container?

## 10. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-10-12 | Initial draft from header excerpts | E-0001..E-0004 |

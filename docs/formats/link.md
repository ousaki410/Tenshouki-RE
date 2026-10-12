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
- Offsets in nested containers appear to be relative to the start of the nested `LINK` (see section 7).

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

## 6. Invariants

Checked with `tools/link_check.py` over all six files (E-0008):

- [x] First entry offset equals `16 + 8 * count` in all six files.
- [x] Between consecutive entries, `next.offset = prev.offset + prev.size + 4` holds for every gap in every file (histogram `{4: n}` in all six).
- [x] In five files (`res_grp`, `res_else`, `res_exp`, `res_exp_pc`, `res_snd`), `last.offset + last.size + 4` equals the file size, so 4 bytes follow the last payload.
- [x] Exception: `res_shader.bin` has six entries where entries 4 and 5 have size 0 and offsets 6288 and 6292, both beyond the file size (6,284). Because the 4-byte gap rule still holds, entry 3 must end exactly at the file size, so the file has 0 trailing bytes. A zero-size entry may therefore lie beyond the end of the file, and the 4-byte tail is not guaranteed.
- [ ] The meaning of the 4-byte gap (padding, checksum, or terminator) is unknown, because no gap bytes have been captured.

## 7. Nested LINK Containers

First payloads of `res_grp`, `res_exp`, and `res_exp_pc` start with a nested `LINK`. Observed nested header words:

| File | +0x04 | +0x08 | +0x0C | +0x10 | Entry at +0x20 (offset, size) | offset + size |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| res_grp entry 0 | 1 | 0x20 | 0x174 (372) | 0x40 | (0x11C0, 0x480070) | 0x481230 = outer size |
| res_exp entry 0 | 1 | 0x20 | 0x6F (111) | 0x40 | (0x580, 0x40038) | 0x405B8 = outer size |
| res_exp_pc entry 0 | 1 | 0x20 | 2 | 0x40 | (0x60, 0x2054) | 0x20B4 = outer size |

Observed in all three: bytes +0x14 to +0x1F are zero, and the entry at +0x20 ends exactly at the outer payload size. This is a different layout from the top-level header.

### Layout hypothesis (fits all three samples)

- +0x08 (0x20) is the offset of a data-entry table.
- +0x0C is the number of records in a descriptor table that starts at +0x10 (0x40).
- Each descriptor record is 12 bytes. In `res_exp` the repeated words `0x14 0x14` and the steps of 0x14 look like sprite rectangles, which is also unverified.
- The data section starts at `(0x40 + 12 * n) rounded down to 16, plus 16`. Check: res_grp 0x40 + 4464 = 0x11B0, giving 0x11C0; res_exp 0x40 + 1332 = 0x574, giving 0x580; res_exp_pc 0x40 + 24 = 0x58, giving 0x60. All three match.

Test: dump the descriptor table of `res_grp` entry 0 (absolute offset 184 + 0x40) and check that every record decodes consistently, then check that the byte at the computed data start is the first payload byte.

## 8. Other Payload Observations

| Payload | Observation | Confidence |
| --- | --- | --- |
| res_else entry 1 at 0x2C | starts with ASCII `LS11`; meaning unknown | observed |
| res_exp_pc nested data start | nested offset 0x60 lands at absolute 0x78, where ASCII `GT1G0600` appears | observed |

## 9. Hypotheses

- `GT1G0600` is the magic of a texture container used by KOEI TECMO titles (commonly called G1T). Test: compare the structure after the magic with public community documentation, without copying code.
- `LS11` is a sub-container or compressed-stream tag. Test: capture a longer excerpt and look for repeating structure.
- The six-entry `res_shader.bin` holds shader sources or binaries, with two empty trailing entries. Test: dump the first 128 bytes.

## 10. Open Questions

- What is stored in the 4-byte gap between payloads?
- Is the nested layout hypothesis in section 7 correct for other nested containers inside `res_grp.bin` (21 entries)?
- What do the descriptor records contain?

## 11. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-10-12 | Initial draft from header excerpts | E-0001..E-0004 |
| 2026-10-12 | Full validation of six files, res_shader exception, nested layout hypothesis | E-0007..E-0010 |

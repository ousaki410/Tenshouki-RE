# LWC Data Blocks

Status: draft
Last updated: 2026-11-10
Parser: partial — `BinArchive::parse_nested` reads the enclosing data table; no LWC decoder yet
Tests: `src/test/test_bin_parser.cpp` (synthetic multi-block container)

## 1. Overview

`LWC` blocks appear as the data entries of four nested `LINK` containers in `res/res_grp.bin`: top-level entries 15, 16, 17, and 20 (see [link.md](link.md) section 7). Each container holds many LWC blocks; the nested data table lists them with `{offset, size}` pairs.

The tag is observed only in these four containers so far. No public documentation has been compared.

## 2. Samples

| Evidence ID | Container | Blocks | Payload bytes | First block | First block header | Block size range |
| --- | --- | ---: | ---: | ---: | --- | --- |
| E-0022 | res_grp entry 15 | 57 | 8,572,537 | 0x4C0 | `4C 57 43 1A 38 00 04 00` | 0x20E3B..0x28DA3 |
| E-0022 | res_grp entry 16 | 856 | 279,971,722 | 0x4300 | `4C 57 43 1A 38 00 08 00` | 0x3DD16..0x55994 |
| E-0022 | res_grp entry 17 | 215 | 33,245,525 | 0x1100 | `4C 57 43 1A 38 00 04 00` | 0x23F45..0x279D8 |
| E-0022 | res_grp entry 20 | 6800 | 15,906,032 | 0x21360 | `4C 57 43 1A 38 40 00 00` | 0x130..0x162A |

All block offsets are 16-byte aligned. The blocks cover 99.98%, 99.99%, 99.98%, and 98.46% of their container payloads respectively; the remainder is alignment padding between blocks. The last block of each container ends exactly at the outer payload size.

Provenance: third-party repackaged distribution. Re-verify against retail files before promoting any field to `confirmed`.

## 3. Block Header

Only the first 8 bytes of each container's first block have been compared. The observed layout is:

| Offset | Size | Type | Observation | Confidence |
| ---: | ---: | --- | --- | --- |
| 0x00 | 4 | char[4] | `LWC` | observed |
| 0x04 | 1 | u8 | `0x1A` in all four first blocks | observed |
| 0x05 | 3 | unknown | `38 00 04` (entries 15, 17), `38 00 08` (entry 16), `38 40 00` (entry 20) | observed |

The three bytes after `0x1A` may encode dimensions or a block subtype; the values are not consistent enough to decode yet. The rest of each block is unparsed.

## 4. Relationship to Descriptor Records

In all four containers, the nested `record_count` equals the number of LWC blocks. Each descriptor record is 12 bytes (`u16 x, y, w, h; u32 last`, see link.md). This suggests one record describes one block, but the fields have not been correlated with block contents.

## 5. Hypotheses

- LWC blocks hold terrain, map, or other large tiled data, one block per descriptor record.
- The `0x1A` byte is a version or block-type tag.
- The three bytes at `0x05..0x07` include a width/height pair; block 20 (`38 40`) differs from blocks 15-17 (`38 00`).

## 6. Open Questions

- What do the bytes at `0x05..0x07` mean?
- How do `x, y, w, h` in the descriptor records map to LWC blocks?
- Is the payload compressed or paletted? Byte entropy has not been measured per block yet.

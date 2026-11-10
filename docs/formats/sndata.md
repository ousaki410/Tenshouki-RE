# Sndata Scenario Data (scenario/sndata1..6.dat)

Status: draft
Last updated: 2026-11-10
Parser: `src/resource/dat_parser.cpp` (`parse_sndata_file` / `parse_sndata_memory`)
Tests: `src/test/test_dat_parser.cpp` (synthetic + real file via `TENSYOKI_DATA_ROOT`)

## 1. Overview

Each `sndata<N>.dat` (N=1..6) is a scenario file containing officer and castle
data in three separate fixed-stride blocks. All multi-byte integers are
little-endian. Text fields are CP932 (Shift-JIS) encoded.

No pixel data or game content is stored in this repository; the parser only
decodes structure and metadata.

## 2. Layout

Observed constants (validated against all six `sndata<N>.dat` files):

| Constant | Value | Meaning |
| ---: | --- | --- |
| `NSLOT` | 532 | officer slots |
| `ATTR_BASE` | 0x0AE8 | officer attribute block start |
| `ATTR_STRIDE` | 47 | bytes per officer attribute record |
| `NAME_BASE` | 0x6C94 | officer name block start |
| `NAME_STRIDE` | 24 | bytes per officer name record |
| `CASTLE_BASE` | 0x9E74 | castle block start |
| `CASTLE_STRIDE` | 33 | bytes per castle record |
| `NCASTLE` | 214 | castle count |

File size is 64,046 bytes for every scenario:
`0x9E74 + 214 * 33 = 47,610` bytes of castle data, with the name block at
`0x6C94 + 532 * 24 = 19,860` and the attribute block at `0x0AE8 + 532 * 47 =
27,792`. The blocks do not overlap; the file contains additional padding.

## 3. Officer Attribute Record (47 bytes)

| Offset | Size | Type | Name | Meaning | Confidence |
| ---: | ---: | --- | --- | --- | --- |
| +0x00 | 18 | u8[] | attr | various officer attributes | observed |
| +0x12 | 2 | u16 | id | officer id; `0xFFFF` marks an empty slot | observed |
| +0x14 | 2 | u8 | pad | unused / reserved | hypothesis |
| +0x16 | 1 | u8 | status | officer status (in service, ronin, dead, …) | observed |
| +0x17 | 1 | u8 | gender | officer gender | observed |
| +0x18 | 23 | u8 | attr2 | remaining attribute bytes | observed |

The officer name is **not** in this record; it lives in the separate name
block (section 4), matched by slot index.

## 4. Officer Name Record (24 bytes)

| Offset | Size | Type | Name | Meaning | Confidence |
| ---: | ---: | --- | --- | --- | --- |
| +0x00 | 18 | char[18] | name | officer name, CP932-encoded, null-padded | observed |
| +0x12 | 6 | u8 | tail | auxiliary bytes | observed |

A slot is empty when `name[0] == 0`.

## 5. Castle Record (33 bytes)

| Offset | Size | Type | Name | Meaning | Confidence |
| ---: | ---: | --- | --- | --- | --- |
| +0x00 | 2 | u16 | header[0] | unknown | observed |
| +0x02 | 2 | u16 | header[1] | unknown | observed |
| +0x04 | 2 | u16 | header[2] | unknown | observed |
| +0x06 | 2 | u16 | header[3] | unknown (often `0xFFFF`) | observed |
| +0x08 | 9 | char[9] | name | castle name, CP932-encoded, null-padded | observed |
| +0x11 | 16 | u8 | tail | auxiliary bytes | observed |

A castle is empty when `name[0] == 0`.

## 6. CP932 Text Decoding

Officer and castle names are stored in CP932 (a Shift-JIS variant). The parser
converts them to UTF-8 via `iconv("UTF-8", "CP932")` when available; otherwise
it falls back to printable-ASCII passthrough. Decoding is best-effort: unknown
byte sequences are rendered as `?`.

## 7. Parser API

`parse_sndata_file(path, scenario)` reads a `.dat` file from disk and returns
a `SndataArchive` containing:

- `officers`: one entry per non-empty slot, with `slot`, `id`, `status`,
  `gender`, `name_raw` (CP932 hex), `name_utf8` (decoded), and the full 47-byte
  `attr` record.
- `castles`: one entry per non-empty castle, with `index`, `header[4]`,
  `name_raw`, `name_utf8`, `tail`, and the full 33-byte `raw` record.

`parse_sndata_memory(data, scenario)` does the same from an in-memory span.

## 8. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-11-10 | Initial sndata parser: officer attributes, officer names, castles | sndata1..6.dat (parse_sndata.py) |

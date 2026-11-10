# Evidence Log

Append-only log of observations that format documents rely on. Do not edit past entries; add a correction entry instead.

## How to record

Collect values with commands such as:

```bash
ls -l <file>
sha256sum <file>
xxd -l 256 <file>
```

Record only what was observed. Keep raw hex excerpts to 64 bytes or fewer.

## Entry template

```
### E-XXXX: <short title>

- Date: YYYY-MM-DD
- Source file: <relative path inside the game directory>
- Size (bytes):
- SHA-256:
- Game version / build:
- Provenance:
- Method: static inspection | differential test | runtime trace
- Command(s):
- Observation:
- Hex excerpt (<= 64 bytes):
- Related format document:
- Result: supports | contradicts | inconclusive
```

## Differential test template

```
### E-XXXX: differential test <what was changed>

- Baseline save/data file: <name>, SHA-256
- Action performed in game: <exact steps>
- Modified file: <name>, SHA-256
- Differing byte ranges: <offset ranges>
- Interpretation:
- Confidence: observed | confirmed
```

## Entries

Common to E-0001..E-0019: date 2026-10-12; method static inspection (`ls`, `sha256sum`, `xxd`, `tools/link_check.py`, `tools/link_nested.py`); provenance is a third-party repackaged distribution, to be re-verified against retail Steam files.

### E-0001: res/res_grp.bin header

- Size: 365,574,996 bytes
- SHA-256: e31700c0262cc6aa82f8b9e6a643fa049e88c2293caadf0f42da99b6bf3ff5d7
- Observation: magic `LINK`, count 21, word 1, word 0; entry table at 0x10 starts with offset 0xB8 (= 16 + 8 * 21); the 13 entries visible in 128 bytes have a constant 4-byte gap between payloads.
- Hex excerpt (first 16 bytes): `4c494e4b 15000000 01000000 00000000`
- Related: link.md
- Result: supports

### E-0002: res/res_else.bin header

- Size: 164,540 bytes
- SHA-256: a939e8fff4912013907c5b09a1e718ea79c27786e7eb6ec6d69e9ad5eb707049
- Observation: `LINK`, count 3; entries (0x28, 0), (0x2C, 0x26BA0), (0x26BD0, 0x16E8); last end + 4 equals the file size; payload at 0x2C starts with ASCII `LS11`.
- Related: link.md
- Result: supports

### E-0003: res/res_exp.bin header

- Size: 263,636 bytes
- SHA-256: b2a1558690c1be07d2052103f8a31b282c4cff68bdac785d2b826e3206e9b173
- Observation: `LINK`, count 1; entry (0x18, 0x405B8); end + 4 equals the file size; payload starts with a nested `LINK` whose first entry (0x580, 0x40038) ends exactly at the outer payload size.
- Related: link.md
- Result: supports

### E-0004: res/res_exp_pc.bin header

- Size: 8,400 bytes
- SHA-256: b776805223fc17ec201cab23f2339db03072ad80a8f86389b5461f6a2ef8cb1f
- Observation: `LINK`, count 1; entry (0x18, 0x20B4); end + 4 equals the file size; nested `LINK` first entry (0x60, 0x2054) points to absolute 0x78 where ASCII `GT1G0600` appears.
- Related: link.md
- Result: supports

### E-0005: data directory inventory

- Observation: `BDAT/01..13.DAT` (2.7 to 9.7 MB), `BGM/BGM00..11.kvs`, `DLC/gf_03200..03259.n6` (60 files, all exactly 16,552 bytes), `MOVIE/*.N6`, `MSG/*.dat` plus `MESSAGE.N6P`, `n6data/HEXMAP.NB6` (241,111 bytes), `n6data/TABLE.N6P` (5,864 bytes), `res/*.bin`, and `scenario/sndata1..6.dat` (all exactly 64,046 bytes) and `taiki1..6.dat`.
- Notes: `MSG/MESSAGE.N6P` and `n6data/MESSAGE.N6P` have the same size (158,624 bytes); content equality is not yet checked.
- Related: dat.md (not started)
- Result: inconclusive

### E-0006: files outside the format scope

- Observation: the directory contains third-party additions: `ali213.bin` (13,674 bytes, no recognizable header), `ALI213.ini`, a launcher executable added by the distributor, a 785,408-byte `steam_api.dll`, `language.dll`, and several `.url` shortcut files.
- Decision: these files are not documented, not analyzed, and excluded from the project. Evidence for `confirmed` status must come from retail files.
- Result: inconclusive

### E-0007: res_shader.bin and res_snd.bin sizes

- Command: `python3 tools/link_check.py "$G"/res/*.bin`
- Observation: `res_shader.bin` is 6,284 bytes with count 6; `res_snd.bin` is 3,914,544 bytes with count 2. Both start with `LINK`, word 1, word 0. SHA-256 values were not captured.
- Related: link.md
- Result: supports

### E-0008: link_check results for all six files

- Observation (count, gap histogram, last end, file size):
  - res_else: 3, {4: 2}, 164536, 164540
  - res_exp: 1, {}, 263632, 263636
  - res_exp_pc: 1, {}, 8396, 8400
  - res_grp: 21, {4: 20}, 365574992, 365574996
  - res_shader: 6, {4: 5}, 6292 (counting empty entries), 6284
  - res_snd: 2, {4: 1}, 3914540, 3914544
- Detail for res_shader: the first version of the script flagged entries 4 and 5 (offset 6288 and 6292, size 0) as out of bounds. The script now allows empty entries beyond EOF.
- Note: the interpretation of `res_shader.bin` first written next to this entry was wrong; see E-0015.
- Related: link.md sections 4 to 6
- Result: supports

### E-0009: nested LINK header of res_grp entry 0

- Source: `xxd -s 184 -l 64 res/res_grp.bin`
- Observation: nested words at +0x04 to +0x10 are 1, 0x20, 0x174, 0x40; bytes +0x14 to +0x1F are zero; the entry at +0x20 is (0x11C0, 0x480070), and 0x11C0 + 0x480070 = 0x481230, which equals the outer entry 0 size.
- Hex excerpt (first 16 bytes): `4c494e4b 01000000 20000000 74010000`
- Related: link.md section 7
- Result: supports

### E-0010: nested layout arithmetic check

- Method: arithmetic over E-0003, E-0004, and E-0009.
- Observation: for the three nested containers, data start equals `(0x40 + 12 * n) rounded down to 16, plus 16` with n = 372, 111, and 2 giving 0x11C0, 0x580, and 0x60.
- Result: supports a hypothesis only.

### E-0011: res_grp entry 0 descriptor records

- Command: `xxd -s 248 -l 64 res/res_grp.bin` (file offset 248 = 184 + 0x40, the descriptor table start)
- Observation: the bytes from 0xF8 decode as 12-byte records (x, y, w, h, last): (528, 452, 33, 33, 0), (92, 620, 10, 33, 0), (102, 620, 13, 33, 0), (115, 620, 10, 33, 0), (812, 604, 10, 33, 0), and the next record begins with x = 822. On the same row, x + w equals the next x.
- Hex excerpt (first 16 bytes): `1002c401 21002100 00000000 5c006c02`
- Related: link.md section 7
- Result: supports

### E-0012: res_grp later descriptor records (offset error in the request)

- Command: `xxd -s 4600 -l 64 res/res_grp.bin`
- Note: this was intended to dump the data start (184 + 0x11C0). That sum is 4,728, not 4,600; the request contained an arithmetic error. The dump instead landed inside the descriptor table.
- Observation: record boundaries from 0xF8 in steps of 12 put record 363 at 0x11FC. Records 363 to 367 decode as (64, 688, 48, 32), (480, 720, 30, 30), (148, 704, 42, 26), (948, 748, 20, 20), (968, 748, 20, 20); 948 + 20 = 968. Records reach x + w = 988 and y + h = 768.
- Related: link.md section 7
- Result: supports

### E-0013: gap bytes in res_exp_pc.bin

- Command: `xxd -s 8396 -l 4 res/res_exp_pc.bin`
- Observation: bytes `ef cd ab 89` (little-endian u32 0x89ABCDEF) at the 4 bytes after the only payload.
- Related: link.md section 6
- Result: supports a sentinel hypothesis (one gap inspected)

### E-0014: res_shader.bin header and first payload

- Command: `xxd -s 0 -l 128 res/res_shader.bin`
- Observation: `LINK`, count 6; entries (0x40, 0xDAC), (0xDF0, 0x428), (0x121C, 0x66C), (0x188C, 0), (0x1890, 0), (0x1894, 0). The first payload at 0x40 begins `00 02 ff ff fe ff fb 01` followed by ASCII `DBUG`, and a path string beginning `C:\Project\` appears at 0x70.
- Related: link.md sections 6 and 8
- Result: supports

### E-0015: correction of the res_shader tail analysis

- Reason: E-0008 and the earlier version of link.md said entry 3 of `res_shader.bin` ends at the file size and that the file has no 4-byte tail. That was inferred from the gap histogram and is wrong.
- Observation (from E-0014): entry 2 is (0x121C, 0x66C) and ends at 0x1888 = 6,280; entry 3 is an empty entry at offset 0x188C = 6,284 (the file size). The file therefore has the same 4-byte tail as the other five files, and entries 3 to 5 are empty.
- Effect: the exception in link.md was removed; all six files satisfy `last non-empty end + 4 = file size`.
- Result: contradicts the earlier interpretation

### E-0016: link_nested output for three containers

- Command: `python3 tools/link_nested.py "$G/res/<file>" 0` for `res_grp.bin`, `res_exp.bin`, `res_exp_pc.bin`
- Observation (n, data entry, descriptor end, predicted data start, actual data start):
  - res_grp: n = 372; data (0x11C0, 0x480070); descriptor end 0x11B0; predicted 0x11C0; actual 0x11C0; tag `GT1G0600`; last values {0: 370, 1: 1, 2: 1}; max x+w 1280; max y+h 775; common sizes (12,12) x54, (3,32) x27, (53,26) x26, (18,18) x21, (4,4) x18, (8,24) x15, (32,32) x12, (30,30) x11.
  - res_exp: n = 111; data (0x580, 0x40038); descriptor end 0x574; predicted 0x580; actual 0x580; tag `GT1G0600`; last values {0: 111}; max x+w 240; max y+h 200; sizes (20,20) x111.
  - res_exp_pc: n = 2; data (0x60, 0x2054); descriptor end 0x58; predicted 0x60; actual 0x60; tag `GT1G0600`; last values {0: 1, 1: 1}; max x+w 32; max y+h 32; sizes (32,32) x2.
- Open point: the 1,280 maximum for `res_grp` entry 0 exceeds the 1,024-wide texture 0 found in E-0017.
- Related: link.md section 7
- Result: supports (one open inconsistency)

### E-0017: GT1G header of res_grp entry 0

- Command: `xxd -s 4728 -l 128 res/res_grp.bin` (4,728 = 184 + 0x11C0)
- Observation: at 0x1278, ASCII `GT1G0600`, u32 0x480070 (equals the nested data size), u32 0x28, u32 3, then 0, 0, then 1, 1, 1; offset table at +0x28: 0x0C, 0x400020, 0x440034. The 20 bytes starting at file offset 0x12AC are `10 01 aa 00 11 11 01 10 0c 00 00 00 00 00 00 00 00 00 00 01`, and pixel data starts at 0x12C0, whose first bytes follow the pattern `xx xx xx ff`.
- Hex excerpt (first 16 bytes): `47543147 30363030 70004800 28000000`
- Related: gt1g.md
- Result: supports

### E-0018: gap sentinel after res_grp entry 0

- Command: `xxd -s 4723432 -l 4 res/res_grp.bin` (4,723,432 = 184 + 0x481230)
- Observation: bytes `ef cd ab 89`.
- Related: link.md section 6
- Result: supports the sentinel hypothesis (two gaps inspected, in two different files)

### E-0019: GT1G size arithmetic

- Method: arithmetic over E-0016 and E-0017.
- Observation: texture 0 is at table offset 0x0C with a 20-byte header, so its pixels start at table offset 0x20 and span until texture 1 at 0x400020, which is exactly 0x400000 = 1024 * 1024 * 4 bytes; byte 0x02 of its entry header is 0xAA (both nibbles 10). Texture 1 to texture 2 spans 0x40014 (0x40000 pixel bytes after a 20-byte header). The remainder after texture 2's header equals 0x40000. The fixed total of 0x70 header bytes equals 28 + 4n + 4n + 20n for n = 3. The same formula gives 0x38 for n = 1 and 0x54 for n = 2, matching `res_exp` and `res_exp_pc`.
- Related: gt1g.md section 7
- Result: supports (textures 1 and 2 headers not read)

### E-0020: parser cross-validation on all six containers

- Date: 2026-11-10
- Source files: `res/res_grp.bin`, `res/res_else.bin`, `res/res_exp.bin`, `res/res_exp_pc.bin`, `res/res_shader.bin`, `res/res_snd.bin`
- Provenance: same third-party repackaged distribution as E-0001..E-0004
- Method: static inspection with independent tools
- Command(s): `python3 tools/verify_resources.py data/res --json report.json`; `TENSYOKI_DATA_ROOT=<game dir> ctest --test-dir build`
- Observation:
  - All six containers PASS the validator: entry counts 21, 3, 1, 1, 6, 2; sentinel counts 21, 2, 1, 1, 3, 2; data coverage 100.00%, 99.97%, 99.99%, 99.67%, 98.79%, 100.00%.
  - The C++ parser reports the same entry counts and the same first-payload offsets (16 + 8 * count) for all six files.
  - Nested LINK containers decode to the documented descriptor counts in the three GT1G-bearing files.
  - `res_grp.bin` entries 6, 7, 8, 9, 10, 13, 15, 16 and 20 have data offsets that differ from the data-start rule by 0x10 or -0x10 (e.g. entry 6: actual 0x60, predicted 0x50). Entry 0 follows the rule exactly.
- Related: link.md sections 6 and 7
- Result: supports the container layout; refines the data-start rule to entry 0 of the GT1G-bearing files only

### E-0021: `_N1G` payload in res_grp entry 1

- Date: 2026-11-10
- Source file: `res/res_grp.bin`
- Size (bytes): 365574996
- SHA-256: e31700c0262cc6aa82f8b9e6a643fa049e88c2293caadf0f42da99b6bf3ff5d7
- Game version / build: HD Power Up Kit, third-party repackaged distribution (same as E-0001..E-0004)
- Provenance: third-party repackaged distribution; re-verify against retail files before decoding
- Method: static inspection
- Command(s): read the entry table, then dump the first 64 bytes of entry 1
- Observation:
  - Entry 1 at 0x4812EC is 3,712,828 bytes and starts with ASCII `_N1G` followed by `0000`.
  - Header u32 values: `0x47314E5F` (`_N1G`), `0x30303030` (`0000`), `0x0038A73C` (= payload size), `0x00000B6C`, `0`, `0x000791CC`, `0x2D`, `3`.
  - `+0x20` begins 0x0B6C, 0x0385B4, 0x0591B4, then `ff ff ff 00`, `ff ff ff 11`, `ff ff ff 20`, `ff ff ff 3A`, `ff ff ff 57`, i.e. white with an increasing alpha byte.
  - Overall byte entropy is 3.54; the first 64 KiB have entropy 1.75.
  - The other 20 entries of `res_grp.bin` start with `LINK`; entry 1 is the only `_N1G` payload in the container.
- Hex excerpt (<= 64 bytes): `5f 4e 31 47 30 30 30 30 3c a7 38 00 6c 0b 00 00 00 00 00 00 cc 91 07 00 2d 00 00 00 03 00 00 00 6c 0b 00 00 b4 85 03 00 b4 91 05 00 ff ff ff 00 ff ff ff 11`
- Related format document: link.md section 8
- Result: inconclusive (tag and header observed; layout not decoded)

### E-0022: nested +0x04 is a data-entry count, not a version

- Date: 2026-11-10
- Source file: `res/res_grp.bin`
- Size (bytes): 365574996
- SHA-256: e31700c0262cc6aa82f8b9e6a643fa049e88c2293caadf0f42da99b6bf3ff5d7
- Game version / build: HD Power Up Kit, third-party repackaged distribution (same as E-0001..E-0004)
- Provenance: third-party repackaged distribution; re-verify against retail files
- Method: static inspection
- Command(s): read the nested header and iterate the data table at +0x08 for every top-level `LINK` payload
- Observation:
  - The u32 at nested +0x04 equals the number of `{offset, size}` pairs in the data table at +0x08. It is 1 for the 16 nested containers that hold one `GT1G` block, and 57, 856, 215, 6800 for top-level entries 15, 16, 17, 20.
  - The data table ends at 0x1E8, 0x1AE0, 0x6D8, 0xD4A0 for entries 15, 16, 17, 20; the descriptor table starts at 0x200, 0x1AE0, 0x6E0, 0xD4A0, so the table never overlaps the descriptors.
  - The descriptor table ends at or before the first data block, and the last data block ends exactly at the outer payload size in all 20 nested containers of `res_grp.bin`.
  - All 57 + 856 + 215 + 6800 data entries of the four multi-block containers start with `LWC 1A` (first block headers `4C 57 43 1A 38 00 04 00`, `4C 57 43 1A 38 00 08 00`, `4C 57 43 1A 38 00 04 00`, `4C 57 43 1A 38 40 00 00`). All block offsets are 16-byte aligned.
  - This contradicts the earlier hypothesis in link.md that +0x04 is a version field fixed at 1; the three GT1G samples happened to have exactly one data block.
- Hex excerpt (<= 64 bytes): `4c 49 4e 4b 39 00 00 00 20 00 00 00 39 00 00 00 00 02 00 00 00 00 00 00` / first LWC block `4c 57 43 1a 38 00 04 00`
- Related format documents: link.md section 7, lwc.md
- Result: contradicts the version hypothesis; supports `data_entry_count` and multi-block data tables

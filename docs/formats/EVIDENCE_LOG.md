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

Common to E-0001..E-0006: date 2026-10-12; method static inspection (`ls`, `sha256sum`, `xxd -l 128`); provenance is a third-party repackaged distribution, to be re-verified against retail Steam files.

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

- Observation: the directory contains third-party additions: `ali213.bin` (13,674 bytes, no recognizable header), `ALI213.ini`, `开始游戏.exe`, a 785,408-byte `steam_api.dll`, `language.dll`, and several `.url` shortcut files.
- Decision: these files are not documented, not analyzed, and excluded from the project. Evidence for `confirmed` status must come from retail files.
- Result: inconclusive

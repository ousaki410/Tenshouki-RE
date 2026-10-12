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

_No entries yet._

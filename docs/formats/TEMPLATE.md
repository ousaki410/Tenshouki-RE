# <Format Name>

Status: not started | draft | stable
Last updated: YYYY-MM-DD
Parser: `src/resource/<file>` (or `none`)
Tests: `src/test/<file>` (or `none`)

## 1. Overview

One paragraph describing what the format stores and where it is used.

## 2. Samples

| Evidence ID | File | Size (bytes) | SHA-256 | Notes |
| --- | --- | ---: | --- | --- |
| E-0001 | | | | |

Evidence IDs refer to entries in `EVIDENCE_LOG.md`.

## 3. Byte Order and Conventions

- Endianness:
- String encoding:
- Alignment:
- Offset base (file start or section start):

## 4. Header Layout

| Offset | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0x00 | | | | | hypothesis | |

## 5. Entry / Record Layout

| Offset | Size | Type | Name | Meaning | Confidence | Evidence |
| ---: | ---: | --- | --- | --- | --- | --- |
| | | | | | | |

## 6. Invariants

Conditions that hold in every observed sample, for example offsets inside file bounds, entries not overlapping, or count times record size equal to table length. Each invariant should map to a validation check in the parser.

- [ ] 

## 7. Hypotheses

Unverified ideas, including how they could be tested.

- 

## 8. Open Questions

- 

## 9. Changelog

| Date | Change | Evidence |
| --- | --- | --- |
| YYYY-MM-DD | Initial draft | |

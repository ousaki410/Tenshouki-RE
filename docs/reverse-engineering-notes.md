# Reverse-Engineering Notes: Nobunaga's Ambition: Tenshouki HD (Power Up Kit)

Status: working notes, 2026-10-12
Scope: Linux-native reimplementation research for a legally purchased copy of the Windows (Steam) release.

## 1. Goal

Evaluate whether the Windows release of *Tenshouki HD with Power Up Kit* can be studied and re-implemented as a Linux-native program, using [REA](https://github.com/morluto/rea) (MCP-based reverse-engineering toolkit) as an investigation aid.

Non-goals:

- Redistributing original executables, assets, keys, or DRM-bypass material.
- Removing or bypassing Steam DRM / executable protection.

## 2. Environment

| Item | Value |
| --- | --- |
| Host OS | Parrot Linux (x86_64) |
| Node.js | v24.21.0 (installed via nvm v0.40.8) |
| npm | v11.19.0 |
| REA | rea-agents 6.4.0 (`npm install --global rea-agents`) |
| Native analysis engine | Not installed yet (Ghidra 12.1.x + full JDK required for native analysis) |

Setup commands:

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.8/install.sh | bash
source ~/.bashrc
nvm install 24
npm install --global rea-agents
rea --help
```

Notes:

- The REA CLI selects the output format with `--format <toon|json|yaml|md|jsonl>`. There is no `--json` flag on the top-level command set.
- REA ships static inspectors that do not need Ghidra, for example `inspect-pe-resources` and `inspect-binary-layout`.
- When Ghidra is needed, REA expects `GHIDRA_INSTALL_DIR` and `JAVA_HOME` to point at existing installations. The upstream documentation was validated against Ghidra 12.1.4 and JDK 21.
- REA's Ghidra provider imports the target into an ephemeral database. It is read-only on Linux apart from function-name and entry-comment annotations, and executable bytes are not modified.
- Ghidra string search only covers data that Ghidra has defined as strings. An empty result does not prove that text is absent.

## 3. Target Files

Game directory: `NOBUNAGA'S AMBITION Tenshouki with Power Up Kit HD version/`

| File | Role (assumed from name) |
| --- | --- |
| `NOBU6HD_JP.exe` | Main game executable |
| `N6HDLauncher_JP.exe` | Launcher |
| `N6HDFaceTool.exe` | Face/portrait tool |
| `KTSYSVIEW/KTSYSVIEW.exe` | System viewer utility |
| `NETSHOW.exe` | Utility executable |

The roles above are inferred from file names only and have not been verified.

## 4. Method: PE Section Entropy Check

A small script (`pecheck.py`) parses the PE section table and computes Shannon entropy per section:

```python
import sys, math, collections
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    pe = int.from_bytes(d[0x3c:0x40], 'little')
    n = int.from_bytes(d[pe+6:pe+8], 'little')
    opt = int.from_bytes(d[pe+20:pe+22], 'little')
    off = pe + 24 + opt
    print("==", p, len(d))
    for i in range(n):
        s = d[off+i*40:off+i*40+40]
        name = s[:8].rstrip(b'\0').decode(errors='replace')
        raw = int.from_bytes(s[20:24], 'little')
        sz = int.from_bytes(s[16:20], 'little')
        b = d[raw:raw+sz]
        c = collections.Counter(b)
        e = -sum(v/len(b)*math.log2(v/len(b)) for v in c.values()) if b else 0
        print(f"  {name:8} size={sz:9} entropy={e:.2f}")
```

Entropy close to 8.00 bits/byte indicates encrypted or compressed content. Typical compiled x86 code is around 6 to 7.

## 5. Results

| File | Size (bytes) | `.text` entropy | `.bind` section | `.bind` size |
| --- | ---: | ---: | --- | ---: |
| `NOBU6HD_JP.exe` | 4,461,944 | 8.00 | present | 423,840 |
| `N6HDLauncher_JP.exe` | 4,800,376 | 8.00 | present | 423,840 |
| `N6HDFaceTool.exe` | 3,946,360 | 8.00 | present | 423,840 |
| `KTSYSVIEW.exe` | 558,040 | 6.60 | absent | n/a |
| `NETSHOW.exe` | 1,058,816 | 6.56 | absent | n/a |

Additional section data for `NOBU6HD_JP.exe`:

| Section | Size | Entropy |
| --- | ---: | ---: |
| `.text` | 2,916,864 | 8.00 |
| `.rdata` | 559,616 | 6.13 |
| `.data` | 145,408 | 4.41 |
| `_RDATA` | 2,048 | 4.80 |
| `.rsrc` | 240,640 | 7.51 |
| `.reloc` | 166,400 | 6.69 |
| `.bind` | 423,840 | 8.00 |

Resource inspection (`rea inspect-pe-resources`) on `N6HDFaceTool.exe` reported 745 resources, mostly with language ID 1041 (Japanese), including icon groups. Payloads other than `RT_GROUP_ICON` are treated as opaque by that inspector.

## 6. Findings and Problems

### 6.1 Main executables are packed

All three `*HD*` executables share the same signature:

- `.text` entropy of 8.00.
- A `.bind` section of exactly 423,840 bytes.

The identical `.bind` size across three different programs indicates the same protection layer applied by the same build pipeline. The `.bind` section name is consistent with a Steam executable wrapper, but this is an inference from PE structure. No tool in this investigation identified the protector by name.

Consequences:

- Static disassembly or decompilation of `.text` would operate on ciphertext. Results from Ghidra would not be meaningful, and REA evidence would not support claims about game logic.
- Native analysis of these three files is blocked until a legitimate unpacked build is available. Removing the protection is out of scope for this project.

### 6.2 Unprotected executables have limited value

`KTSYSVIEW.exe` and `NETSHOW.exe` are not packed, so they can be used to exercise the REA and Ghidra workflow. They are utilities and are not expected to contain core game logic.

### 6.3 Signature search returned nothing

Searches for the `LWC\x1a` byte signature returned no offsets in any of the five executables. This does not rule out the signature in data archives.

### 6.4 Repository state

An initial review of this repository found that the code base is a pre-alpha skeleton. Items reported by the review (to be re-verified against current `master`):

- Calling code references headers and APIs that do not match the current declarations.
- `Renderer` and `ShaderCompiler` have headers without implementations.
- Dear ImGui is referenced as a CMake subproject, but upstream does not provide a root CMake target.
- The BIN loader may pass a span over a buffer that the next call clears.
- Tests are not wired into CTest and target an older API.

## 7. Revised Strategy

Because executable analysis is blocked, the project focuses on data and behavior rather than code:

1. **Data formats first.** Reverse-engineer BIN, GRP, and DAT containers by hex inspection and differential testing (change one value in game, save, diff before and after). Record file sizes, SHA-256, header fields, entry counts, and boundary offsets for every confirmed structure.
2. **Black-box behavior.** Run the game under Wine/Proton and trace file access order and sizes. REA process-capture commands record terminal output, interactions, exit status, and filesystem observations without reading code.
3. **Cross-validation.** Use community documentation to cross-check field meanings. Do not copy code from repositories that lack an explicit license.
4. **Baseline.** Verify the game runs acceptably under Proton on Linux. This may already satisfy the practical goal.
5. **Native vertical slice.** After formats are verified, implement a loader that imports the user's own assets, then a minimal slice: map, castle, and officer display, one command, one turn advance, save and load, compared against the original field by field.

## 8. Open Items

- Provide `ls -lR` of the data directory with sizes.
- Provide `xxd -l 256` for each container type (BIN, GRP, DAT).
- Install Ghidra and JDK only if an unprotected target is available.
- Re-verify the repository issues in section 6.4 against the current commit.

## 9. Legal and Ethical Boundaries

- Analyze only a legally purchased copy.
- Do not distribute original executables, game assets, keys, emulators for platform DRM, or circumvention tools.
- Interoperability exceptions are narrow and jurisdiction-dependent; this document is not legal advice.

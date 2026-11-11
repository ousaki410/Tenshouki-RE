# Tensyoki — Linux Port

A native Linux port of *Nobunaga's Ambition: Tenshouki* (信長の野望・天翔記) HD Edition.

## Status

**Phase 1 (architecture skeleton) — complete** ✅
- Core modules: configuration, logging, VFS, math utilities
- Platform layer: SDL3 + OpenGL 4.5 window/input/clipboard
- Resource parsers: `.bin` containers (`BinArchive`), `.grp` images, `.dat` structured data
- Rendering abstraction: OpenGL backend, sprite/text renderer framework
- Debug UI: ImGui resource browser, hex viewer

**Phase 2 (clean build / baseline validation) — complete** ✅
- Build system, API drift and ownership issues fixed
- `BinArchive` rewritten to the documented LINK layout (16-byte header, 8-byte table,
  `0x89ABCDEF` sentinels, nested LINK with multi-block GT1G/LWC data);
  `tools/verify_resources.py` passes all six `res/*.bin` containers
- Data layer: `SndataArchive` parser for scenario/sndata1..6.dat (officers,
  castles, CP932 name decoding); 4 unit tests + real-file integration tests

**Phase 3 (data layer + rendering core) — complete** ✅
- Officers / castles data layer (see docs/formats/sndata.md)
- Renderer core: texture upload from GT1G blocks, sprite rendering, text rendering
- CJK text rendering via FreeType, GT1G texture cache, shader compilation

**Phase 4 (campaign layer) — complete** ✅
- Game state machine, main menu, strategic map, castle commands, tactical battle
- Castle loyalty/tax/unrest, rebellion events, facilities, save/load slots

**Phase 5 (officer management) — complete** ✅
- Officer browser/detail UI, lord assignment, battle-map integration
- Headless suites for battle, castle, save, and officer subsystems

**Phase 6 (turn-based strategy layer) — complete** ✅
- Eight regional factions with AI economy, recruitment, construction, and attacks
- Monthly turns, seasonal food modifiers, statistical combat, victory/defeat
- Ownership-aware map, strategy event log, save format v2
- AI officer assignment: each AI faction appoints lords to its castles each turn
- See [docs/STRATEGY_LAYER.md](docs/STRATEGY_LAYER.md)

**Next** 🔲
- Region table: sndata castle record (33 bytes) carries no region field
  (verified across sndata1..6.dat); keep `castle.index % 8` placeholder until
  map/scenario data provides a verified per-castle region table
- Diplomacy, tactical AI, and campaign balancing

## Game files

**This repository does not contain the game.** All game assets are excluded because individual files exceed GitHub's 100 MB per-file limit.

To run the port you need your own legally obtained copy of the game. Place it in the repository root using the original directory name:

```
NOBUNAGA'S AMBITION Tenshouki with Power Up Kit HD version/
├── res/
│   ├── res_grp.bin
│   ├── res_shader.bin
│   └── res_else.bin
├── MSG/
├── scenario/
└── ...
```

The path can also be supplied on the command line or via an environment variable (see [Running](#running)).

## Build requirements

- CMake 3.20+
- A C++20 compiler (GCC 11+, Clang 13+, MSVC 19.30+)
- SDL3 development libraries
- spdlog, fmt, glm (fetched automatically via CMake `FetchContent`)
- DirectX Shader Compiler (DXC) — for HLSL → SPIR-V
- An OpenGL 4.5+ / Vulkan 1.2+ driver

### Ubuntu/Debian
```bash
sudo apt install cmake g++ libsdl3-dev libvulkan-dev
```

### Arch Linux
```bash
sudo pacman -S cmake gcc sdl3 vulkan-headers vulkan-icd-loader
```

## Building

```bash
# Configure
cmake -S . -B build -G Ninja

# Compile
cmake --build build

# Run unit tests
ctest --test-dir build --output-on-failure

# Headless subsystem suites (no window, no GPU required)
./build/tensyoki --test-battle
./build/tensyoki --test-castle
./build/tensyoki --test-save
./build/tensyoki --test-officer
./build/tensyoki --test-strategy

# Include the integration tests that read the real containers
TENSYOKI_DATA_ROOT="/path/to/game/dir" ctest --test-dir build --output-on-failure
```

> **Note on the `build/` directory**
> `build/` is a local build tree and is not tracked by Git. Run the configuration step above to
> generate it. Vendored dependency sources under `build/_deps/*-src/` are git-ignored and are
> re-fetched by `FetchContent`.

## Running

```bash
# Pass the game data directory explicitly
./build/tensyoki "/path/to/NOBUNAGA'S AMBITION Tenshouki with Power Up Kit HD version"

# Or set an environment variable
export TENSYOKI_DATA_ROOT=/path/to/game/data
./build/tensyoki

# Headless self-check: parse all six containers, decode nested LINK/GT1G/LWC, exit
./build/tensyoki --check-resources "/path/to/game/dir"
```

## Project layout

```
src/
├── app/              # Application entry point (planned)
├── core/             # Core utilities
│   ├── config.hpp/cpp      # TOML configuration
│   ├── filesystem.hpp/cpp  # Virtual file system
│   ├── logging.hpp/cpp     # spdlog wrapper
│   └── math_utils.hpp/cpp  # Math helpers
├── platform/         # Platform abstraction
│   ├── platform.hpp        # Interface
│   └── platform_linux.cpp  # SDL3 Linux implementation
├── render/           # Rendering abstraction
│   ├── renderer.hpp/cpp    # Renderer core (OpenGL/Vulkan)
│   └── shader_compiler.hpp/cpp  # DXC HLSL → SPIR-V
├── resource/         # Resource parsers
│   ├── bin_parser.hpp/cpp  # .bin containers (LINK magic)
│   ├── grp_parser.hpp/cpp  # Image decoding
│   └── dat_parser.hpp/cpp  # Structured arrays (officers/castles)
├── game/             # Campaign layer
│   ├── game_state.hpp/cpp       # State machine
│   ├── game_states.hpp/cpp      # Menu, map, castle, battle, officer states
│   ├── map_view.hpp/cpp         # Strategic map rendering and interaction
│   ├── castle_command.hpp/cpp   # Castle economy and commands
│   ├── battle.hpp/cpp           # Tactical battle system
│   ├── strategy.hpp/cpp         # Turn-based factions, AI, victory/defeat
│   └── save_system.hpp/cpp      # Save slots (text format, version 2)
├── ui/               # Debug UI
│   └── imgui_backend.hpp/cpp  # ImGui SDL3 + OpenGL backend
└── test/             # Unit tests
```

Other directories:

- `docs/` — reverse-engineering notes and file format documentation
- `tools/` — standalone Python scripts for inspecting and extracting game data
- `out/` — analysis results (officer attributes, `sndata` JSON dumps, reports)

## Debug UI

- **F1** — show/hide the resource browser
- **Esc** — quit
- **F11** — toggle fullscreen

Resource browser features:
- Select a `.bin` container (`res_shader.bin`, `res_else.bin`, `res_grp.bin`, …)
- Filter and browse entries as a tree
- Inspect metadata (offset, size, type, hash)
- Hex dump (first 256 bytes / full entry)
- Extract a single entry or all entries to the `extracted/` directory

## License

MIT License — see [LICENSE](LICENSE).

## Acknowledgements

- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform windowing and input
- [Dear ImGui](https://github.com/ocornut/imgui) — immediate-mode GUI
- [spdlog](https://github.com/gabime/spdlog) — fast logging
- [fmt](https://github.com/fmtlib/fmt) — formatting library
- [GLM](https://github.com/g-truc/glm) — math library
- [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) — HLSL compiler

---

*This project is intended for learning and research only. It does not include any original game assets.*

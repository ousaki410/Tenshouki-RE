# Tensyoki — Linux Port

A native Linux port of *Nobunaga's Ambition: Tenshouki* (信長の野望・天翔記) HD Edition.

## Status

**Phase 1 (architecture skeleton) — complete** ✅
- Core modules: configuration, logging, VFS, math utilities
- Platform layer: SDL3 + OpenGL 4.5 window/input/clipboard
- Resource parsers: `.bin` containers (`BinArchive`), `.grp` images, `.dat` structured data
- Rendering abstraction: OpenGL backend, sprite/text renderer framework
- Debug UI: ImGui resource browser, hex viewer

**Phase 2 (clean build / baseline validation) — in progress** 🔧
- Fixing the build system, API drift, and ownership issues

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

# Run tests
ctest --test-dir build --output-on-failure
```

> **Note on the committed `build/` directory**
> The repository includes a snapshot of the build tree for reference. It contains absolute paths from the
> original machine, so it cannot be reused directly — run the configuration step above to generate a
> working build. Vendored dependency sources under `build/_deps/*-src/` are git-ignored and are
> re-fetched by `FetchContent`.

## Running

```bash
# Pass the game data directory explicitly
./build/tensyoki "/path/to/NOBUNAGA'S AMBITION Tenshouki with Power Up Kit HD version"

# Or set an environment variable
export TENSYOKI_DATA_ROOT=/path/to/game/data
./build/tensyoki
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

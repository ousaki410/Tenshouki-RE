cmake_minimum_required(VERSION 3.20)
project(tensyoki VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion")
    # Sanitizers for debug builds
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer")
endif()

# Fetch dependencies via FetchContent
include(FetchContent)

# Fetch SDL3
FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sdl3)

# Fetch spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# Fetch fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

# Fetch toml++
FetchContent_Declare(
    toml
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(toml)

# Fetch glm
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# Fetch Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Find system packages
find_package(OpenGL REQUIRED)

# Optional: Vulkan (for future use)
find_package(Vulkan QUIET)

# Fetch glad for OpenGL loading - use newer version compatible with CMake 4.x
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
    GIT_SHALLOW TRUE
)
# Set CMake policy before FetchContent_MakeAvailable
cmake_policy(SET CMP0155 NEW)
# Configure GLAD for OpenGL 4.5 Core Profile
set(GLAD_API "gl=4.5" CACHE STRING "OpenGL API version")
set(GLAD_PROFILE "core" CACHE STRING "OpenGL profile")
set(GLAD_GENERATOR "c" CACHE STRING "Language to generate the binding for")
set(GLAD_SPEC "gl" CACHE STRING "Name of the spec")
FetchContent_MakeAvailable(glad)

# Optional: DirectXShaderCompiler (for HLSL→SPIR-V)
# We'll provide a minimal fallback if not available
find_package(DirectXShaderCompiler CONFIG QUIET)
if(NOT DXC_FOUND)
    message(STATUS "DirectXShaderCompiler not found, using minimal fallback")
    add_library(dxc::dxcompiler INTERFACE IMPORTED)
    set_target_properties(dxc::dxcompiler PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
endif()

# Create ImGui static library with backends
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glad_SOURCE_DIR}/include
)

target_link_libraries(imgui_lib PUBLIC
    SDL3::SDL3
    OpenGL::GL
    glad
)

# Add executable
add_executable(tensyoki)
target_sources(tensyoki PRIVATE
    src/main.cpp
    src/core/config.cpp
    src/core/filesystem.cpp
    src/core/logging.cpp
    src/core/math_utils.cpp
    src/platform/platform_linux.cpp
    src/render/renderer.cpp
    src/render/shader_compiler.cpp
    src/resource/bin_parser.cpp
    src/resource/grp_parser.cpp
    src/resource/dat_parser.cpp
    src/ui/imgui_backend.cpp
    src/ui/texture_atlas_viewer.hpp
)

target_include_directories(tensyoki PRIVATE
    src
    src/core
    src/ui
    ${glad_SOURCE_DIR}/include
    $<TARGET_PROPERTY:glad,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(tensyoki PRIVATE
    SDL3::SDL3
    spdlog::spdlog
    fmt::fmt
    tomlplusplus::tomlplusplus
    dxc::dxcompiler
    glm::glm
    OpenGL::GL
    imgui_lib
    glad
)

# Enable testing
enable_testing()
add_subdirectory(src/test)

# Install
install(TARGETS tensyoki DESTINATION bin)
# ZEngine — Build System and Distribution

**Priority:** P3 — Required for reproducible builds and Steam distribution
**Status:** Design
**Depends on:** All subsystems (this is a cross-cutting concern)
**Blocks:** Shipping, CI/CD

---

## Table of Contents

1. [CMake Version and Toolchain Requirements](#1-cmake-version-and-toolchain-requirements)
2. [Project Structure — CMake Target Layout](#2-project-structure--cmake-target-layout)
3. [External Dependencies — Vendoring Strategy](#3-external-dependencies--vendoring-strategy)
4. [Build Configurations](#4-build-configurations)
5. [Platform-Specific Link Targets](#5-platform-specific-link-targets)
6. [Shader Compilation Step](#6-shader-compilation-step)
7. [Asset Cook Step](#7-asset-cook-step)
8. [CPack Packaging](#8-cpack-packaging)
9. [CI/CD Pipeline — GitHub Actions](#9-cicd-pipeline--github-actions)
10. [Symbol Management](#10-symbol-management)
11. [Version Numbering](#11-version-numbering)
12. [Deliverables Checklist](#12-deliverables-checklist)

---

## 1. CMake Version and Toolchain Requirements

### Minimum versions

| Tool | Minimum | Notes |
|------|---------|-------|
| CMake | 3.25 | Required for `FetchContent_MakeAvailable`, generator expressions in `install()`, `cmake_path()` |
| MSVC | 19.37 (VS 2022 17.7) | C++20 modules support, `std::ranges`, `std::format` |
| GCC | 13.1 | Full `<ranges>`, `<expected>`, coroutine support |
| Clang | 16.0 | Full C++20 including `std::format` |
| Vulkan SDK | 1.3.268 | Minimum for `VK_KHR_dynamic_rendering` and `VK_EXT_descriptor_buffer` |
| Ninja | 1.11 | Recommended generator on all platforms for fast incremental builds |
| Python | 3.10+ | Required by some Vulkan SDK tools and build scripts |

### Root CMakeLists.txt — preamble

```cmake
cmake_minimum_required(VERSION 3.25)

project(ZEngine
    VERSION 1.0.0
    DESCRIPTION "ZEngine — C++20 Vulkan Game Engine"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------
# Global C++ standard
# -----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # No GNU extensions; pure ISO C++20

# -----------------------------------------------------------------------
# Policy settings
# -----------------------------------------------------------------------
cmake_policy(SET CMP0091 NEW)   # MSVC runtime library via MSVC_RUNTIME_LIBRARY
cmake_policy(SET CMP0135 NEW)   # FetchContent timestamp policy
cmake_policy(SET CMP0077 NEW)   # option() honours parent cache variables

# -----------------------------------------------------------------------
# Build type default
# -----------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo"
        CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "RelWithDebInfo" "Release")
endif()
```

### Toolchain files

Platform-specific flags and overrides are isolated in toolchain files. Pass them to CMake with `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-msvc.cmake`.

**`cmake/toolchains/windows-msvc.cmake`:**

```cmake
# Statically link the MSVC CRT in Release to avoid redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreadedDebugDLL>")

# Enable whole-program optimization in Release.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Use /utf-8 everywhere to avoid encoding issues with MSVC.
add_compile_options(/utf-8)
```

**`cmake/toolchains/linux-gcc.cmake`:**

```cmake
set(CMAKE_C_COMPILER   gcc-13)
set(CMAKE_CXX_COMPILER g++-13)

# Export all symbols so backtrace() can resolve function names without .debug files.
add_link_options(-rdynamic)

# LTO in Release.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
```

**`cmake/toolchains/macos-clang.cmake`:**

```cmake
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

# Minimum deployment target.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")

# Universal binary if needed; default to native arch.
# set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

# LTO in Release.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
```

---

## 2. Project Structure — CMake Target Layout

### Target hierarchy

```
ZEngine (STATIC library)      — engine core: ECS, renderer, audio, physics, etc.
ZGame   (SHARED library)      — game module DLL loaded at runtime by the editor and runtime
ZEditor (EXECUTABLE)          — editor shell (ZENGINE_EDITOR=1), links ZEngine + ZGame
ZRuntime (EXECUTABLE)         — shipping runtime, no editor code
ZTests  (EXECUTABLE)          — GTest unit test suite
ZCook   (EXECUTABLE)          — headless asset cook pipeline (build-time tool only)
```

### Directory layout

```
ZEngine/
├── CMakeLists.txt                     # root
├── cmake/
│   ├── toolchains/
│   │   ├── windows-msvc.cmake
│   │   ├── linux-gcc.cmake
│   │   └── macos-clang.cmake
│   ├── CompilerOptions.cmake          # reusable warning/optimisation flags
│   ├── Dependencies.cmake             # all FetchContent / find_package calls
│   └── Packaging.cmake                # CPack configuration
├── __externals/                       # git submodules + vendored headers
├── ZEngine/                           # engine core source
│   └── CMakeLists.txt
├── ZGame/                             # game module
│   └── CMakeLists.txt
├── ZEditor/                           # editor executable
│   └── CMakeLists.txt
├── ZRuntime/                          # shipping runtime executable
│   └── CMakeLists.txt
├── ZTests/                            # unit tests
│   └── CMakeLists.txt
└── ZCook/                             # asset cook tool
    └── CMakeLists.txt
```

### Root CMakeLists.txt — subdirectory inclusion

```cmake
# -----------------------------------------------------------------------
# Engine-wide options
# -----------------------------------------------------------------------
option(ZENGINE_EDITOR    "Build the editor"            ON)
option(ZENGINE_TESTS     "Build unit tests"            ON)
option(ZENGINE_PROFILING "Enable Tracy profiler"       OFF)
option(ZENGINE_STEAM     "Enable Steamworks SDK"       OFF)
option(ZENGINE_CRASH_UPLOAD "Enable crash uploading"  OFF)

# -----------------------------------------------------------------------
# Load dependencies first (FetchContent, find_package)
# -----------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# -----------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------
add_subdirectory(ZEngine)     # static lib — always built

if(ZENGINE_EDITOR)
    add_subdirectory(ZEditor)
    add_subdirectory(ZGame)   # game DLL needed by editor
endif()

add_subdirectory(ZRuntime)    # shipping runtime — always built

if(ZENGINE_TESTS)
    enable_testing()
    add_subdirectory(ZTests)
endif()

add_subdirectory(ZCook)       # cook tool — always built (used by CookAssets target)
```

### ZEngine/CMakeLists.txt (engine static library)

```cmake
add_library(ZEngine STATIC)

# Glob is explicitly avoided; list every file.
target_sources(ZEngine
    PRIVATE
        Core/Memory/ArenaAllocator.cpp
        Core/Containers/String.cpp
        ECS/EntityRegistry.cpp
        ECS/Scene.cpp
        Renderer/Vulkan/VulkanContext.cpp
        Renderer/Vulkan/VulkanSwapchain.cpp
        Renderer/RenderGraph/RenderGraph.cpp
        Audio/AudioEngine.cpp
        Physics/PhysicsWorld.cpp
        VFS/VirtualFileSystem.cpp
        CrashHandler/CrashHandler.cpp
        # ... (all .cpp files listed explicitly)
)

target_include_directories(ZEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}       # "ZEngine/Core/..." includes
        ${CMAKE_SOURCE_DIR}/__externals   # external headers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..    # internal use only
)

target_compile_definitions(ZEngine
    PUBLIC
        $<$<CONFIG:Debug>:ZENGINE_DEBUG=1>
        $<$<CONFIG:RelWithDebInfo>:ZENGINE_RELWITHDEBINFO=1>
        $<$<CONFIG:Release>:ZENGINE_RELEASE=1>
        $<$<BOOL:${ZENGINE_EDITOR}>:ZENGINE_EDITOR=1>
        $<$<BOOL:${ZENGINE_PROFILING}>:ZENGINE_PROFILING=1>
        $<$<BOOL:${ZENGINE_STEAM}>:ZENGINE_STEAM=1>
)

target_link_libraries(ZEngine
    PUBLIC
        Vulkan::Vulkan
        glfw
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-hlsl
        VulkanMemoryAllocator
    PRIVATE
        $<$<BOOL:${ZENGINE_PROFILING}>:TracyClient>
)

# Platform link targets (see Section 5 for full detail).
if(WIN32)
    target_link_libraries(ZEngine PRIVATE DbgHelp)
elseif(APPLE)
    target_link_libraries(ZEngine PRIVATE "-lobjc")
    target_link_libraries(ZEngine PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework CoreServices"
    )
else() # Linux
    target_link_libraries(ZEngine PRIVATE dl pthread)
endif()

# Include platform-specific source.
if(WIN32)
    target_sources(ZEngine PRIVATE
        CrashHandler/CrashHandlerWindows.cpp
        Platform/Win32/Win32Window.cpp
    )
elseif(APPLE)
    target_sources(ZEngine PRIVATE
        CrashHandler/CrashHandlerMacOS.cpp
        Platform/macOS/CocoaWindow.mm
    )
else()
    target_sources(ZEngine PRIVATE
        CrashHandler/CrashHandlerLinux.cpp
        Platform/Linux/X11Window.cpp
    )
endif()
```

### ZEditor/CMakeLists.txt

```cmake
add_executable(ZEditor)

target_sources(ZEditor
    PRIVATE
        Main.cpp
        EditorApp.cpp
        Panels/SceneHierarchyPanel.cpp
        Panels/ContentBrowserPanel.cpp
        # ...
)

target_include_directories(ZEditor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(ZEditor
    PRIVATE
        ZENGINE_EDITOR=1
)

target_link_libraries(ZEditor
    PRIVATE
        ZEngine
        imgui
        ImGuizmo
        yaml-cpp          # editor-only serialisation
        $<$<BOOL:${ZENGINE_STEAM}>:SteamworksSDK>
)
```

### ZRuntime/CMakeLists.txt

```cmake
add_executable(ZRuntime)

target_sources(ZRuntime
    PRIVATE
        Main.cpp
        RuntimeApp.cpp
)

target_link_libraries(ZRuntime
    PRIVATE
        ZEngine
        ZGame         # game DLL if shipping monolithically; otherwise delay-load
        $<$<BOOL:${ZENGINE_STEAM}>:SteamworksSDK>
)

# Windows: embed a manifest for UAC and DPI awareness.
if(WIN32)
    target_sources(ZRuntime PRIVATE Resources/ZRuntime.manifest)
endif()
```

### ZTests/CMakeLists.txt

```cmake
add_executable(ZTests)

target_sources(ZTests
    PRIVATE
        Tests/Core/ArenaAllocatorTest.cpp
        Tests/ECS/EntityRegistryTest.cpp
        Tests/VFS/VirtualFileSystemTest.cpp
        Tests/Renderer/RenderGraphTest.cpp
        # ...
)

target_link_libraries(ZTests
    PRIVATE
        ZEngine
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(ZTests)
```

---

## 3. External Dependencies — Vendoring Strategy

All external dependency logic lives in `cmake/Dependencies.cmake`. It is included before any `add_subdirectory` call.

```cmake
# cmake/Dependencies.cmake
include(FetchContent)

# -----------------------------------------------------------------------
# Vulkan SDK — system install required (not vendored).
# -----------------------------------------------------------------------
# Minimum required version. The SDK sets Vulkan_FOUND and Vulkan::Vulkan.
find_package(Vulkan 1.3.268 REQUIRED)
message(STATUS "Vulkan SDK: ${Vulkan_VERSION} at ${Vulkan_INCLUDE_DIRS}")

# -----------------------------------------------------------------------
# GLFW — FetchContent, zlib/libpng license.
# -----------------------------------------------------------------------
# We build only the library; disable examples, tests, and docs.
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4            # pinned version
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)
# Link target: glfw

# -----------------------------------------------------------------------
# SPIRV-Cross — git submodule at __externals/SPIRV-Cross.
# Apache 2.0 license.
# -----------------------------------------------------------------------
# Disable the SPIRV-Cross standalone CLI and tests.
set(SPIRV_CROSS_CLI          OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/__externals/SPIRV-Cross
                 ${CMAKE_BINARY_DIR}/__externals/SPIRV-Cross
                 EXCLUDE_FROM_ALL)
# Link targets: spirv-cross-core, spirv-cross-glsl, spirv-cross-hlsl

# -----------------------------------------------------------------------
# ImGui — vendored copy at __externals/imgui.
# MIT license.
# -----------------------------------------------------------------------
add_library(imgui STATIC
    __externals/imgui/imgui.cpp
    __externals/imgui/imgui_draw.cpp
    __externals/imgui/imgui_tables.cpp
    __externals/imgui/imgui_widgets.cpp
    __externals/imgui/backends/imgui_impl_glfw.cpp
    __externals/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui
    PUBLIC
        __externals/imgui
        __externals/imgui/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)
# Link target: imgui

# -----------------------------------------------------------------------
# ImGuizmo — vendored copy at __externals/ImGuizmo.
# MIT license.
# -----------------------------------------------------------------------
add_library(ImGuizmo STATIC
    __externals/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(ImGuizmo PUBLIC __externals/ImGuizmo)
target_link_libraries(ImGuizmo PUBLIC imgui)
# Link target: ImGuizmo

# -----------------------------------------------------------------------
# VulkanMemoryAllocator — git submodule at __externals/VulkanMemoryAllocator.
# MIT license.
# -----------------------------------------------------------------------
# VMA is a header-only library with an optional implementation compilation unit.
add_library(VulkanMemoryAllocator STATIC
    __externals/VulkanMemoryAllocator/src/VmaUsage.cpp
)
target_include_directories(VulkanMemoryAllocator
    PUBLIC  __externals/VulkanMemoryAllocator/include
)
target_link_libraries(VulkanMemoryAllocator PUBLIC Vulkan::Vulkan)
# Link target: VulkanMemoryAllocator

# -----------------------------------------------------------------------
# assimp — git submodule at __externals/assimp.
# BSD 3-Clause license.
# Disable importers not needed by ZEngine to reduce build time.
# -----------------------------------------------------------------------
set(ASSIMP_BUILD_TESTS             OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS      OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                 OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT               ON  CACHE BOOL "" FORCE)
# Keep only the importers we actually use.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER      ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER     ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER      ON  CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/__externals/assimp
                 ${CMAKE_BINARY_DIR}/__externals/assimp
                 EXCLUDE_FROM_ALL)
# Link target: assimp

# -----------------------------------------------------------------------
# JoltPhysics — git submodule at __externals/JoltPhysics.
# MIT license.
# -----------------------------------------------------------------------
set(TARGET_HELLO_WORLD             OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS              OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST        OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/__externals/JoltPhysics/Build
                 ${CMAKE_BINARY_DIR}/__externals/JoltPhysics
                 EXCLUDE_FROM_ALL)
# Link target: Jolt

# -----------------------------------------------------------------------
# miniaudio — single-header vendored at __externals/miniaudio/miniaudio.h.
# Unlicense / MIT dual license.
# -----------------------------------------------------------------------
# Compiled as a static library from the implementation compilation unit.
add_library(miniaudio STATIC
    __externals/miniaudio/miniaudio_impl.cpp   # created by ZEngine team:
                                               # #define MINIAUDIO_IMPLEMENTATION
                                               # #include "miniaudio.h"
)
target_include_directories(miniaudio PUBLIC __externals/miniaudio)
# Link target: miniaudio

# -----------------------------------------------------------------------
# nlohmann_json — single-header at __externals/nlohmann/json.hpp.
# MIT license. Header-only, no compilation needed.
# -----------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE __externals/nlohmann)
# Link target: nlohmann_json

# -----------------------------------------------------------------------
# yaml-cpp — FetchContent, editor-only. MIT license.
# -----------------------------------------------------------------------
if(ZENGINE_EDITOR)
    set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(yaml-cpp)
    # Link target: yaml-cpp
endif()

# -----------------------------------------------------------------------
# Tracy — FetchContent, profiling builds only. MIT license.
# -----------------------------------------------------------------------
if(ZENGINE_PROFILING)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.10
        GIT_SHALLOW    TRUE
    )
    set(TRACY_ENABLE  ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)   # only profile when viewer connects
    FetchContent_MakeAvailable(tracy)
    # Link target: TracyClient
endif()

# -----------------------------------------------------------------------
# shaderc — prefer system Vulkan SDK, fall back to FetchContent.
# Apache 2.0 license.
# -----------------------------------------------------------------------
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslangValidator.exe
    PATHS ${Vulkan_INCLUDE_DIRS}/../bin
    REQUIRED
)
message(STATUS "glslangValidator: ${GLSLANG_VALIDATOR}")

# shaderc runtime library (for runtime shader compilation in the editor).
if(ZENGINE_EDITOR)
    find_library(SHADERC_LIB
        NAMES shaderc_combined shaderc
        PATHS ${Vulkan_INCLUDE_DIRS}/../lib
    )
    if(SHADERC_LIB)
        add_library(shaderc INTERFACE)
        target_include_directories(shaderc INTERFACE ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(shaderc INTERFACE ${SHADERC_LIB})
        message(STATUS "shaderc: ${SHADERC_LIB}")
    else()
        message(STATUS "shaderc not found in Vulkan SDK, fetching...")
        FetchContent_Declare(shaderc
            GIT_REPOSITORY https://github.com/google/shaderc.git
            GIT_TAG        v2023.6
            GIT_SHALLOW    TRUE
        )
        set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(shaderc)
    endif()
endif()

# -----------------------------------------------------------------------
# stb — single headers vendored at __externals/stb/.
# Public domain / MIT license.
# -----------------------------------------------------------------------
# stb_image, stb_truetype, stb_image_write — compiled in one unit.
add_library(stb STATIC __externals/stb/stb_impl.cpp)
target_include_directories(stb PUBLIC __externals/stb)
# Link target: stb

# -----------------------------------------------------------------------
# msdfgen + msdf-atlas-gen — build-time tool only (not linked into runtime).
# MIT license.
# -----------------------------------------------------------------------
# Built as a standalone executable used by ZCook for SDF font atlas generation.
# Not added to ZEngine's link dependencies.
set(MSDFGEN_BUILD_STANDALONE ON  CACHE BOOL "" FORCE)
set(MSDF_ATLAS_BUILD_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/__externals/msdf-atlas-gen
                 ${CMAKE_BINARY_DIR}/__externals/msdf-atlas-gen
                 EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------
# GTest — FetchContent, test builds only. BSD 3-Clause license.
# -----------------------------------------------------------------------
if(ZENGINE_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    # Link target: GTest::gtest_main
endif()

# -----------------------------------------------------------------------
# Steamworks SDK — vendored at __externals/steamworks/. PROPRIETARY.
# Include only when ZENGINE_STEAM is ON.
# The SDK is not redistributable. Do not commit to a public repository.
# -----------------------------------------------------------------------
if(ZENGINE_STEAM)
    add_library(SteamworksSDK INTERFACE)
    target_include_directories(SteamworksSDK
        INTERFACE __externals/steamworks/public/steam
    )
    if(WIN32)
        target_link_libraries(SteamworksSDK
            INTERFACE
                ${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/win64/steam_api64.lib
        )
    elseif(APPLE)
        target_link_libraries(SteamworksSDK
            INTERFACE
                ${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/osx/libsteam_api.dylib
        )
    else()
        target_link_libraries(SteamworksSDK
            INTERFACE
                ${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/linux64/libsteam_api.so
        )
    endif()
    # Link target: SteamworksSDK
endif()
```

---

## 4. Build Configurations

### Compile options by configuration

All compile options are defined in `cmake/CompilerOptions.cmake` and applied via a helper function:

```cmake
# cmake/CompilerOptions.cmake

function(zengine_apply_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4             # warning level 4
            /WX             # warnings as errors
            /MP             # multi-processor compilation
            /permissive-    # strict standard conformance
            /Zc:preprocessor  # conforming preprocessor (required for __VA_OPT__)
            $<$<CONFIG:Debug>:
                /Od             # no optimization
                /RTC1           # runtime error checks
                /MDd            # debug CRT DLL
                /Zi             # debug info (PDB)
                /fsanitize=address  # ASAN (VS 2022 built-in)
            >
            $<$<CONFIG:RelWithDebInfo>:
                /O2             # speed optimization
                /Zi             # debug info
                /MD             # release CRT DLL
                /GL             # whole-program optimization
            >
            $<$<CONFIG:Release>:
                /O3             # max optimization (requires /Ox + /Ob2)
                /Ob3            # aggressive inlining
                /MD
                /GL
                /GS-            # disable security cookie (perf, shipping only)
            >
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:/LTCG /DEBUG /OPT:REF /OPT:ICF>
            $<$<CONFIG:Release>:      /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Werror
            -fno-exceptions      # ZEngine does not use C++ exceptions
            -fno-rtti            # ZEngine uses its own RTTI
            $<$<CONFIG:Debug>:
                -O0
                -g3
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
                -fstack-protector-strong
            >
            $<$<CONFIG:RelWithDebInfo>:
                -O2
                -g
                -fno-omit-frame-pointer   # required for good stack traces
                $<$<CXX_COMPILER_ID:GNU>:-fprofile-abs-path>
            >
            $<$<CONFIG:Release>:
                -O3
                -DNDEBUG
                -fomit-frame-pointer
                -flto
                $<$<CXX_COMPILER_ID:GNU>:-fuse-linker-plugin>
            >
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Release>:-flto -Wl,--gc-sections>
        )
    endif()
endfunction()
```

Call `zengine_apply_compile_options(ZEngine)` at the end of each target's `CMakeLists.txt`.

### Configuration-specific preprocessor definitions

| Macro | Debug | RelWithDebInfo | Release |
|-------|-------|----------------|---------|
| `NDEBUG` | — | defined | defined |
| `ZENGINE_DEBUG` | 1 | — | — |
| `ZENGINE_RELWITHDEBINFO` | — | 1 | — |
| `ZENGINE_RELEASE` | — | — | 1 |
| `ZENGINE_PROFILING` | 1 (if option ON) | 1 (if option ON) | 0 |
| `ZENGINE_STEAM` | — | — | 1 (if option ON) |
| `ZENGINE_CRASH_HANDLER_ENABLED` | — | 1 | 1 |

---

## 5. Platform-Specific Link Targets

```cmake
# Applied at the end of ZEngine/CMakeLists.txt and the executable targets.

if(WIN32)
    target_link_libraries(ZEngine PRIVATE
        DbgHelp         # crash handler: MiniDumpWriteDump, stack walking
        Winmm           # audio: timeBeginPeriod (timer resolution)
        ws2_32          # networking (future)
        Shlwapi         # path utilities
    )
    if(ZENGINE_STEAM)
        target_link_libraries(ZRuntime PRIVATE
            ${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/win64/steam_api64.lib
        )
        # Copy steam_api64.dll to the output directory post-build.
        add_custom_command(TARGET ZRuntime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/win64/steam_api64.dll"
                "$<TARGET_FILE_DIR:ZRuntime>"
        )
    endif()

elseif(APPLE)
    target_link_libraries(ZEngine PRIVATE
        "-framework CoreAudio"          # audio engine
        "-framework AudioToolbox"       # audio format conversion
        "-framework CoreFoundation"     # property lists, bundles
        "-framework CoreServices"       # FSEvents file watching
        "-framework IOKit"              # hardware info (for crash log)
        "-lobjc"                        # crash handler: NSAlert via ObjC runtime
    )
    if(ZENGINE_STEAM)
        target_link_libraries(ZRuntime PRIVATE
            "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/osx/libsteam_api.dylib"
        )
        # Ensure the dylib is found via @executable_path at runtime.
        set_target_properties(ZRuntime PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks"
            BUILD_RPATH   "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/osx"
        )
    endif()

else() # Linux
    target_link_libraries(ZEngine PRIVATE
        dl              # dlopen for dynamic loading (crash dialog GTK, curl)
        pthread         # threading
        asound          # ALSA audio backend
    )
    if(ZENGINE_STEAM)
        target_link_libraries(ZRuntime PRIVATE
            "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/linux64/libsteam_api.so"
        )
        set_target_properties(ZRuntime PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_RPATH   "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/linux64"
        )
    endif()
endif()
```

---

## 6. Shader Compilation Step

SPIR-V shader binaries are produced at build time from GLSL source files located in `Assets/Shaders/`. The compiled SPIR-V files are written to `${CMAKE_BINARY_DIR}/Shaders/` and then copied to the runtime asset directory during the cook step.

### CMake custom command — per-shader compilation

```cmake
# cmake/ShaderCompilation.cmake

function(zengine_compile_shaders target shader_source_dir spirv_output_dir)
    # Find all shader source files.
    file(GLOB_RECURSE SHADER_SOURCES
        "${shader_source_dir}/*.vert"
        "${shader_source_dir}/*.frag"
        "${shader_source_dir}/*.comp"
        "${shader_source_dir}/*.geom"
        "${shader_source_dir}/*.tesc"
        "${shader_source_dir}/*.tese"
        "${shader_source_dir}/*.mesh"
        "${shader_source_dir}/*.task"
        "${shader_source_dir}/*.rgen"
        "${shader_source_dir}/*.rchit"
        "${shader_source_dir}/*.rmiss"
    )

    set(SPIRV_OUTPUTS "")

    foreach(SHADER ${SHADER_SOURCES})
        # Compute relative path to preserve subdirectory structure.
        cmake_path(RELATIVE_PATH SHADER
            BASE_DIRECTORY "${shader_source_dir}"
            OUTPUT_VARIABLE SHADER_REL)

        set(SPIRV_FILE "${spirv_output_dir}/${SHADER_REL}.spv")

        # Ensure the output subdirectory exists.
        get_filename_component(SPIRV_DIR "${SPIRV_FILE}" DIRECTORY)
        file(MAKE_DIRECTORY "${SPIRV_DIR}")

        add_custom_command(
            OUTPUT  "${SPIRV_FILE}"
            COMMAND "${GLSLANG_VALIDATOR}"
                    --target-env vulkan1.3
                    -V                       # Vulkan semantics
                    --auto-map-bindings      # auto-assign descriptor set bindings
                    -o "${SPIRV_FILE}"
                    "${SHADER}"
            DEPENDS "${SHADER}"
            COMMENT "Compiling shader: ${SHADER_REL}"
            VERBATIM
        )

        list(APPEND SPIRV_OUTPUTS "${SPIRV_FILE}")
    endforeach()

    # Create a custom target that depends on all SPIR-V outputs.
    add_custom_target(ZEngineShaders ALL DEPENDS ${SPIRV_OUTPUTS})

    # The main target depends on shaders being compiled first.
    add_dependencies(${target} ZEngineShaders)
endfunction()
```

Usage in `ZEngine/CMakeLists.txt`:

```cmake
include(${CMAKE_SOURCE_DIR}/cmake/ShaderCompilation.cmake)

zengine_compile_shaders(
    ZEngine
    "${CMAKE_SOURCE_DIR}/Assets/Shaders"
    "${CMAKE_BINARY_DIR}/Shaders"
)
```

This approach is incremental: `add_custom_command` records each SPIR-V file as depending on its GLSL source. Only shaders newer than their `.spv` output are recompiled on the next build invocation.

### Debug information in shaders

For Debug and RelWithDebInfo builds, add the `-g` flag to `glslangValidator` to embed SPIR-V debug info (source line numbers), which is consumed by RenderDoc for shader debugging:

```cmake
set(GLSLANG_DEBUG_FLAGS "")
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
    set(GLSLANG_DEBUG_FLAGS "-g" "--enhanced-msgs")
endif()

add_custom_command(
    # ...
    COMMAND "${GLSLANG_VALIDATOR}"
            --target-env vulkan1.3 -V
            ${GLSLANG_DEBUG_FLAGS}
            -o "${SPIRV_FILE}" "${SHADER}"
    # ...
)
```

---

## 7. Asset Cook Step

The asset cook pipeline is implemented as a standalone executable `ZCook` (in `ZCook/CMakeLists.txt`). It processes raw assets (textures, meshes, audio files, fonts) into engine-ready cooked formats, and is invoked as a post-build step from CMake.

### Custom target: CookAssets

```cmake
# Included at the bottom of the root CMakeLists.txt.

# Determine the cooked asset output directory.
set(COOKED_ASSETS_DIR "${CMAKE_BINARY_DIR}/CookedAssets")

# Collect all raw asset files. We glob these for dependency tracking.
file(GLOB_RECURSE RAW_ASSETS
    "${CMAKE_SOURCE_DIR}/Assets/**/*.png"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.jpg"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.hdr"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.fbx"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.gltf"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.glb"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.wav"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.ogg"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.ttf"
    "${CMAKE_SOURCE_DIR}/Assets/**/*.otf"
)

# Stamp file: exists and is newer than all raw assets when the cook is up to date.
set(COOK_STAMP "${CMAKE_BINARY_DIR}/cook.stamp")

add_custom_command(
    OUTPUT  "${COOK_STAMP}"
    COMMAND $<TARGET_FILE:ZCook>
                --source   "${CMAKE_SOURCE_DIR}/Assets"
                --output   "${COOKED_ASSETS_DIR}"
                --jobs     "${CMAKE_BUILD_PARALLEL_LEVEL}"
                --verbose
    COMMAND ${CMAKE_COMMAND} -E touch "${COOK_STAMP}"
    DEPENDS ZCook ${RAW_ASSETS}
    COMMENT "Cooking assets..."
    VERBATIM
)

add_custom_target(CookAssets
    DEPENDS "${COOK_STAMP}"
)

# ZRuntime and ZEditor depend on cooked assets being up to date.
add_dependencies(ZRuntime CookAssets)
if(ZENGINE_EDITOR)
    add_dependencies(ZEditor CookAssets)
endif()
```

### Incremental cook logic

`ZCook` itself implements incremental cooking via an asset manifest: a JSON file (`CookedAssets/manifest.json`) that records the SHA-256 hash of each source file alongside its output path and cook parameters. On each invocation, `ZCook` checks whether the source hash has changed before re-cooking. Assets whose source is unchanged and whose output file exists are skipped. The CMake stamp file prevents `CookAssets` from running at all if no raw assets have changed since the last successful cook.

### Cook configuration

Cook parameters (e.g., texture compression format, mip level count, audio bitrate) are read from `Assets/cook_config.yaml`, which is source-controlled alongside the assets. This allows artists to configure per-asset quality settings without touching CMakeLists files.

---

## 8. CPack Packaging

CPack configuration lives in `cmake/Packaging.cmake`, included from the root `CMakeLists.txt` at the bottom.

```cmake
# cmake/Packaging.cmake
include(CPack)
include(InstallRequiredSystemLibraries)

# -----------------------------------------------------------------------
# Common CPack settings
# -----------------------------------------------------------------------
set(CPACK_PACKAGE_NAME              "GameName")
set(CPACK_PACKAGE_VENDOR            "Your Studio Name")
set(CPACK_PACKAGE_DESCRIPTION_SHORT "GameName — A ZEngine-powered game")
set(CPACK_PACKAGE_VERSION_MAJOR     ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR     ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH     ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GameName")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE.txt")

# Install the runtime executable and cooked assets.
install(TARGETS ZRuntime
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .
)
install(DIRECTORY "${CMAKE_BINARY_DIR}/CookedAssets/"
    DESTINATION "assets"
)
```

### 8.1 Windows — NSIS Installer or ZIP

```cmake
if(WIN32)
    # Primary packaging format: NSIS installer.
    set(CPACK_GENERATOR "NSIS;ZIP")

    set(CPACK_NSIS_DISPLAY_NAME       "GameName ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME       "GameName")
    set(CPACK_NSIS_MUI_ICON           "${CMAKE_SOURCE_DIR}/Resources/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON        "${CMAKE_SOURCE_DIR}/Resources/icon.ico")
    set(CPACK_NSIS_INSTALL_ROOT       "$PROGRAMFILES64")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\GameName.lnk' '$INSTDIR\\\\bin\\\\ZRuntime.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\GameName.lnk'")
    # Run the game directly after installation.
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\ZRuntime.exe")

    # Install the MSVC C++ redistributable (static CRT in Release mitigates this).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    include(InstallRequiredSystemLibraries)

    # Include steam_api64.dll alongside the executable.
    if(ZENGINE_STEAM)
        install(FILES
            "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/win64/steam_api64.dll"
            DESTINATION bin
        )
    endif()

    # Include Vulkan runtime DLL if shipping a loader (not required when the
    # VulkanSDK is installed on the target machine; include for robustness).
    # install(FILES "${Vulkan_INCLUDE_DIRS}/../bin/vulkan-1.dll" DESTINATION bin)
endif()
```

**Produce the installer:**
```sh
cd build && cpack -G NSIS
# Output: GameName-1.0.0-win64.exe

cd build && cpack -G ZIP
# Output: GameName-1.0.0-win64.zip
```

### 8.2 Linux — AppImage

AppImage is the recommended Linux distribution format because it is self-contained (no installation required, works on any distro with glibc >= the build target version).

```cmake
if(UNIX AND NOT APPLE)
    # CPack produces a directory tree that the AppImage tool wraps.
    set(CPACK_GENERATOR "External")
    set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT
        "${CMAKE_SOURCE_DIR}/cmake/AppImagePackage.cmake")

    if(ZENGINE_STEAM)
        install(FILES
            "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/linux64/libsteam_api.so"
            DESTINATION lib
        )
    endif()
endif()
```

`cmake/AppImagePackage.cmake` (invoked by CPack External):

```cmake
# AppImagePackage.cmake — called by CPack with CPACK_TEMPORARY_DIRECTORY set.
set(APP_DIR "${CPACK_TEMPORARY_DIRECTORY}/AppDir")

# Write the .desktop file.
file(WRITE "${APP_DIR}/GameName.desktop"
    "[Desktop Entry]\n"
    "Name=GameName\n"
    "Exec=ZRuntime\n"
    "Icon=gamename\n"
    "Type=Application\n"
    "Categories=Game;\n"
)
file(COPY "${CMAKE_SOURCE_DIR}/Resources/icon.png"
    DESTINATION "${APP_DIR}"
    FILES_MATCHING PATTERN "*.png"
)

# Run appimagetool (must be on PATH or at a known location).
find_program(APPIMAGETOOL appimagetool REQUIRED)
execute_process(
    COMMAND "${APPIMAGETOOL}" "${APP_DIR}"
            "${CPACK_PACKAGE_DIRECTORY}/GameName-${CPACK_PACKAGE_VERSION}-x86_64.AppImage"
    RESULT_VARIABLE _result
)
if(_result)
    message(FATAL_ERROR "appimagetool failed: ${_result}")
endif()
```

The AppImage bundles `libsteam_api.so` under `AppDir/usr/lib/`, which is prepended to `LD_LIBRARY_PATH` by the AppImage runtime.

### 8.3 macOS — .app Bundle + Notarization

```cmake
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")   # produces a .dmg

    # Configure the .app bundle properties.
    set_target_properties(ZRuntime PROPERTIES
        MACOSX_BUNDLE              TRUE
        MACOSX_BUNDLE_INFO_PLIST   "${CMAKE_SOURCE_DIR}/Resources/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.yourstudio.gamename"
        MACOSX_BUNDLE_BUNDLE_NAME  "GameName"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE    "AppIcon"
    )

    # Copy dylib dependencies into the Frameworks directory.
    set_target_properties(ZRuntime PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
    )

    if(ZENGINE_STEAM)
        install(FILES
            "${CMAKE_SOURCE_DIR}/__externals/steamworks/redistributable_bin/osx/libsteam_api.dylib"
            DESTINATION "GameName.app/Contents/Frameworks"
        )
    endif()

    # Icon.
    install(FILES "${CMAKE_SOURCE_DIR}/Resources/AppIcon.icns"
        DESTINATION "GameName.app/Contents/Resources"
    )

    # CPack DragNDrop settings.
    set(CPACK_DMG_VOLUME_NAME       "GameName ${PROJECT_VERSION}")
    set(CPACK_DMG_BACKGROUND_IMAGE  "${CMAKE_SOURCE_DIR}/Resources/dmg_background.png")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT
        "${CMAKE_SOURCE_DIR}/cmake/dmg_setup.applescript")
endif()
```

**Code signing and notarization** (post-CPack shell script `scripts/sign_and_notarize.sh`):

```sh
#!/bin/bash
set -euo pipefail

APP="build/GameName-${VERSION}-Darwin/GameName.app"
DMG="build/GameName-${VERSION}-Darwin.dmg"
CERT_ID="${APPLE_DEVELOPER_CERT_ID}"   # from CI environment
APPLE_ID="${APPLE_ID}"
APP_PASSWORD="${APPLE_APP_PASSWORD}"
TEAM_ID="${APPLE_TEAM_ID}"

# 1. Deep-sign the .app bundle.
codesign --force --deep --sign "${CERT_ID}" \
    --options runtime \
    --entitlements "Resources/entitlements.plist" \
    "${APP}"

# 2. Verify signing.
codesign --verify --deep --strict "${APP}"
spctl --assess --type exec "${APP}"

# 3. Re-create the DMG after signing (CPack produced it before signing).
hdiutil create -volname "GameName" -srcfolder "${APP}" \
    -ov -format UDZO "${DMG}"
codesign --sign "${CERT_ID}" "${DMG}"

# 4. Notarize with Apple.
xcrun notarytool submit "${DMG}" \
    --apple-id  "${APPLE_ID}" \
    --password  "${APP_PASSWORD}" \
    --team-id   "${TEAM_ID}" \
    --wait

# 5. Staple the notarization ticket.
xcrun stapler staple "${DMG}"
```

---

### 8.5 ZEngine SDK Package — for engine programmers and designers

In addition to the game installer (NSIS/AppImage/DMG for end users), the build system
produces a **ZEngine SDK package** that is distributed to engine programmers and
designers. This is what game teams install to develop games — NOT the engine source.

**SDK package contents:**

```
ZEngineSDK_v{VERSION}/
  bin/
    Panzerfaust           # project launcher (.exe on Windows, binary on macOS/Linux)
    Obelisk               # engine entry point
    cmake/                # bundled CMake — no system install required
      bin/
        cmake             # (cmake.exe on Windows)
        cpack
      share/
        cmake-3.x/        # CMake modules
  PluginSDK/
    include/
      PluginSDK.h         # the ONLY engine header game DLLs may include
      PluginTypes.h
      PluginECS.h
      PluginRenderGraph.h
      PluginImporter.h
      PluginEditor.h
      PluginAllocator.h
    CMakeLists.txt        # starter CMake for a new game DLL
    Templates/
      Blank/
        Source/
          CMakeLists.txt  # pre-configured to build MyGame.dll
          GameEntry.cpp   # ZGame_* entry points
        Scripts/
          starter.lua
        projectConfig.json
      3D_Starter/
        ...
      Multiplayer_Starter/
        ...
  Docs/
    GettingStarted.md
    LuaAPIReference.md
    PluginSDKReference.md
  LICENSE
  CHANGELOG.md
```

**CMake target for SDK package:**

```cmake
# cmake/Packaging.cmake — SDK package (separate from game installer)
if(ZENGINE_BUILD_SDK)
    # Bundle CMake binary alongside the SDK
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/cmake/bundled/cmake/"
        DESTINATION "bin/cmake"
        COMPONENT SDK
    )
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/PluginSDK/"
        DESTINATION "PluginSDK"
        COMPONENT SDK
    )
    install(PROGRAMS
        "$<TARGET_FILE:Panzerfaust>"
        "$<TARGET_FILE:Obelisk>"
        DESTINATION "bin"
        COMPONENT SDK
    )
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/Templates/"
        DESTINATION "PluginSDK/Templates"
        COMPONENT SDK
    )

    set(CPACK_COMPONENTS_ALL SDK)
    set(CPACK_COMPONENT_SDK_DISPLAY_NAME "ZEngine SDK")
    set(CPACK_COMPONENT_SDK_DESCRIPTION
        "Engine SDK for game development — no source code required")
endif()
```

**Bundling CMake:**

CMake binaries are available as portable archives from cmake.org (MIT license).
Download the appropriate archive for each platform at CI time and place it at
`cmake/bundled/cmake/`:

```yaml
# .github/workflows/build.yml — download bundled CMake
- name: Download bundled CMake for SDK
  run: |
    # macOS arm64
    curl -L https://github.com/Kitware/CMake/releases/download/v3.29.0/cmake-3.29.0-macos-universal.tar.gz \
         -o cmake-bundle.tar.gz
    tar -xzf cmake-bundle.tar.gz --strip-components=1 -C cmake/bundled/cmake/
```

**What the SDK user (engine programmer) must install themselves:**
- A C++ compiler: Visual Studio Build Tools (Windows), Xcode CLI (macOS), GCC/Clang (Linux)
- Nothing else — CMake is bundled

**What the SDK user (Lua designer) must install:**
- Nothing — Panzerfaust opens the editor, Lua scripts reload on save

---

## 9. CI/CD Pipeline — GitHub Actions

The full workflow file is at `.github/workflows/build.yml`.

```yaml
# .github/workflows/build.yml
name: ZEngine CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main, develop ]

# Cancel in-progress runs for the same branch/PR.
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build:
    name: Build (${{ matrix.os }} / ${{ matrix.build_type }})
    runs-on: ${{ matrix.os }}

    strategy:
      fail-fast: false
      matrix:
        os:          [ windows-latest, ubuntu-24.04, macos-14 ]
        build_type:  [ Debug, RelWithDebInfo, Release ]
        include:
          # Windows uses MSVC via the pre-installed VS 2022 toolchain.
          - os: windows-latest
            cmake_toolchain: cmake/toolchains/windows-msvc.cmake
            cmake_generator: "Visual Studio 17 2022"
            cmake_arch:      "-A x64"
            package_ext:     "exe"
          # Ubuntu uses GCC 13.
          - os: ubuntu-24.04
            cmake_toolchain: cmake/toolchains/linux-gcc.cmake
            cmake_generator: "Ninja"
            cmake_arch:      ""
            package_ext:     "AppImage"
          # macOS uses Apple Clang on the M1 runner.
          - os: macos-14
            cmake_toolchain: cmake/toolchains/macos-clang.cmake
            cmake_generator: "Ninja"
            cmake_arch:      ""
            package_ext:     "dmg"

    steps:
      # ------------------------------------------------------------------
      # Checkout
      # ------------------------------------------------------------------
      - name: Checkout (with submodules)
        uses: actions/checkout@v4
        with:
          submodules: recursive
          fetch-depth: 0    # full history for version numbering

      # ------------------------------------------------------------------
      # Caching
      # ------------------------------------------------------------------
      - name: Cache CMake build directory
        uses: actions/cache@v4
        with:
          path: build
          key: cmake-${{ matrix.os }}-${{ matrix.build_type }}-${{ hashFiles('CMakeLists.txt', 'cmake/**', '__externals/**/*.cmake') }}
          restore-keys: |
            cmake-${{ matrix.os }}-${{ matrix.build_type }}-

      - name: Cache FetchContent downloads
        uses: actions/cache@v4
        with:
          path: ~/.cmake/fetch-content
          key: fetchcontent-${{ matrix.os }}-${{ hashFiles('cmake/Dependencies.cmake') }}
          restore-keys: |
            fetchcontent-${{ matrix.os }}-

      # ------------------------------------------------------------------
      # Toolchain setup
      # ------------------------------------------------------------------
      - name: Install Ninja (Linux/macOS)
        if: runner.os != 'Windows'
        run: |
          if [[ "${{ runner.os }}" == "Linux" ]]; then
            sudo apt-get install -y ninja-build
          else
            brew install ninja
          fi

      - name: Install GCC 13 (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
          sudo apt-get update
          sudo apt-get install -y gcc-13 g++-13

      - name: Install Linux system libraries
        if: runner.os == 'Linux'
        run: |
          sudo apt-get install -y \
            libwayland-dev \
            libxkbcommon-dev \
            xorg-dev \
            libasound2-dev \
            libpulse-dev \
            libgtk-3-dev

      # ------------------------------------------------------------------
      # Vulkan SDK
      # ------------------------------------------------------------------
      - name: Install Vulkan SDK (Windows)
        if: runner.os == 'Windows'
        run: |
          $version = "1.3.290.0"
          $url = "https://sdk.lunarg.com/sdk/download/$version/windows/VulkanSDK-$version-Installer.exe"
          Invoke-WebRequest -Uri $url -OutFile VulkanSDK.exe
          Start-Process VulkanSDK.exe -ArgumentList "--accept-licenses --default-answer --confirm-command install" -Wait
          echo "VULKAN_SDK=C:\VulkanSDK\$version" >> $env:GITHUB_ENV
          echo "C:\VulkanSDK\$version\Bin" >> $env:GITHUB_PATH

      - name: Install Vulkan SDK (Linux)
        if: runner.os == 'Linux'
        run: |
          VERSION="1.3.290"
          wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
          sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-$VERSION-noble.list \
            https://packages.lunarg.com/vulkan/$VERSION/lunarg-vulkan-$VERSION-noble.list
          sudo apt-get update
          sudo apt-get install -y vulkan-sdk
          echo "VULKAN_SDK=/usr" >> $GITHUB_ENV

      - name: Install Vulkan SDK (macOS)
        if: runner.os == 'macOS'
        run: |
          VERSION="1.3.290.0"
          URL="https://sdk.lunarg.com/sdk/download/$VERSION/mac/vulkansdk-macos-$VERSION.dmg"
          curl -L -o VulkanSDK.dmg "$URL"
          hdiutil attach VulkanSDK.dmg -mountpoint /Volumes/VulkanSDK
          sudo /Volumes/VulkanSDK/InstallVulkan.app/Contents/MacOS/InstallVulkan \
            --root ~/VulkanSDK --accept-licenses --default-answer --confirm-command install
          hdiutil detach /Volumes/VulkanSDK
          echo "VULKAN_SDK=$HOME/VulkanSDK/$VERSION/macOS" >> $GITHUB_ENV
          echo "$HOME/VulkanSDK/$VERSION/macOS/bin" >> $GITHUB_PATH

      # ------------------------------------------------------------------
      # Configure
      # ------------------------------------------------------------------
      - name: Configure CMake
        run: |
          cmake -B build \
            -G "${{ matrix.cmake_generator }}" \
            ${{ matrix.cmake_arch }} \
            -DCMAKE_TOOLCHAIN_FILE="${{ matrix.cmake_toolchain }}" \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DCMAKE_FETCHCONTENT_BASE_DIR="$HOME/.cmake/fetch-content" \
            -DZENGINE_EDITOR=${{ matrix.build_type != 'Release' && 'ON' || 'OFF' }} \
            -DZENGINE_TESTS=ON \
            -DZENGINE_PROFILING=${{ matrix.build_type == 'Debug' && 'ON' || 'OFF' }} \
            -DZENGINE_STEAM=OFF

      # ------------------------------------------------------------------
      # Build
      # ------------------------------------------------------------------
      - name: Build
        run: cmake --build build --config ${{ matrix.build_type }} --parallel

      # ------------------------------------------------------------------
      # Test
      # ------------------------------------------------------------------
      - name: Run Tests
        working-directory: build
        run: ctest -C ${{ matrix.build_type }} --output-on-failure --parallel 4

      # ------------------------------------------------------------------
      # Package (Release builds only)
      # ------------------------------------------------------------------
      - name: Package
        if: matrix.build_type == 'Release' && github.ref == 'refs/heads/main'
        working-directory: build
        run: cpack -C Release

      - name: Upload Package Artifact
        if: matrix.build_type == 'Release' && github.ref == 'refs/heads/main'
        uses: actions/upload-artifact@v4
        with:
          name: ZEngine-${{ matrix.os }}-Release
          path: |
            build/*.exe
            build/*.AppImage
            build/*.dmg
            build/*.zip
          retention-days: 30

      # ------------------------------------------------------------------
      # Symbol upload (Release builds on main branch only)
      # ------------------------------------------------------------------
      - name: Upload Symbols to Sentry
        if: >
          matrix.build_type == 'Release' &&
          github.ref == 'refs/heads/main' &&
          env.SENTRY_AUTH_TOKEN != ''
        env:
          SENTRY_AUTH_TOKEN: ${{ secrets.SENTRY_AUTH_TOKEN }}
          SENTRY_ORG:        ${{ secrets.SENTRY_ORG }}
          SENTRY_PROJECT:    ${{ secrets.SENTRY_PROJECT }}
        run: |
          curl -sL https://sentry.io/get-cli/ | sh
          sentry-cli upload-dif \
            --org  "$SENTRY_ORG" \
            --project "$SENTRY_PROJECT" \
            build/
```

### Cache strategy notes

- The CMake build directory cache (`build/`) is keyed on `CMakeLists.txt`, all files under `cmake/`, and `*.cmake` files under `__externals/`. This ensures the cache is invalidated when any dependency version changes.
- FetchContent downloads (`~/.cmake/fetch-content`) are cached separately, keyed only on `cmake/Dependencies.cmake`. This cache survives CMake reconfiguration and significantly speeds up CI.
- The caches are per-OS and per-build-type so Debug and Release builds don't stomp each other.

---

## 10. Symbol Management

Symbol management is a post-deploy, not post-build, concern. It runs once per shipped build, triggered by the CI/CD release pipeline.

### Windows — PDB upload

PDB files are generated by MSVC for all non-Debug builds. They are archived as CI artifacts (see Section 9) and uploaded to Sentry after the build succeeds:

```sh
# scripts/upload_symbols_windows.sh (runs in CI release pipeline)
sentry-cli difutil check build/ZRuntime.pdb build/ZEngine.pdb
sentry-cli upload-dif \
    --org   "$SENTRY_ORG" \
    --project "$SENTRY_PROJECT" \
    --include-sources \
    build/*.pdb
```

A private symbol server (Microsoft `symstore.exe` or `mspdbsrv`) is an alternative for studios that do not use Sentry. Store PDBs indexed by the GUID+Age embedded in each binary:

```bat
REM Windows batch — add to symbol store
symstore.exe add /r /f "build\*.pdb" /s "\\symbol-server\symbols" /t "GameName" /v "1.0.0"
```

### Linux — DWARF / Breakpad .sym

Use Google Breakpad's `dump_syms` tool (part of Breakpad's `src/tools/linux/`) to extract a `.sym` file from the ELF binary:

```sh
dump_syms build/ZRuntime > build/ZRuntime.sym
# Upload to Sentry or a self-hosted Breakpad symbol server.
sentry-cli upload-dif --org "$SENTRY_ORG" --project "$SENTRY_PROJECT" build/ZRuntime
```

Alternatively, use `eu-strip` from elfutils to produce a sidecar `.debug` file:
```sh
eu-strip --strip-debug -f build/ZRuntime.debug build/ZRuntime
```

### macOS — dSYM bundle

The `dSYM` bundle is produced alongside the `.app` bundle when `-g` is in the compile flags. Upload via Sentry:

```sh
sentry-cli upload-dif \
    --org "$SENTRY_ORG" --project "$SENTRY_PROJECT" \
    build/ZRuntime.app.dSYM
```

---

## 11. Version Numbering

### Version in CMakeLists.txt

The authoritative version lives in the root `CMakeLists.txt` `project()` call:

```cmake
project(ZEngine
    VERSION 1.0.0   # MAJOR.MINOR.PATCH
    LANGUAGES CXX
)
```

CMake automatically populates `PROJECT_VERSION_MAJOR`, `PROJECT_VERSION_MINOR`, `PROJECT_VERSION_PATCH`.

### configure_file → EngineVersion.h

CMake's `configure_file` substitutes version variables into a C++ header at configure time:

```cmake
# In root CMakeLists.txt, after project():
configure_file(
    "${CMAKE_SOURCE_DIR}/ZEngine/Core/EngineVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/EngineVersion.h"
    @ONLY
)

# Make the generated header visible to all targets.
include_directories("${CMAKE_BINARY_DIR}/generated")
```

**`ZEngine/Core/EngineVersion.h.in`:**

```cpp
// Auto-generated by CMake — do not edit.
// Source: ZEngine/Core/EngineVersion.h.in
#pragma once

#define ZENGINE_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define ZENGINE_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define ZENGINE_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define ZENGINE_VERSION_STRING "@PROJECT_VERSION@"

// Steam build ID — injected by the Steam partner upload tool via environment variable.
// Falls back to "0" for non-Steam builds and local development.
#define ZENGINE_STEAM_BUILD_ID "@ZENGINE_STEAM_BUILD_ID@"
```

The Steam build ID is passed in by the CI/CD release pipeline:

```cmake
# In root CMakeLists.txt — read from environment if available.
if(DEFINED ENV{STEAM_BUILD_ID})
    set(ZENGINE_STEAM_BUILD_ID $ENV{STEAM_BUILD_ID})
else()
    set(ZENGINE_STEAM_BUILD_ID "0")
endif()
```

In the Steam partner upload pipeline (Steamworks ContentBuilder), `STEAM_BUILD_ID` is set to the Steam build ID of the uploaded depot. This allows the crash handler to include the exact Steam build in crash reports, enabling Sentry to group crashes by build.

### Usage in engine code

```cpp
// ZEngine/Core/Engine.cpp
#include "EngineVersion.h"

void Engine::LogVersion() {
    ZENGINE_CORE_INFO(
        "ZEngine v" ZENGINE_VERSION_STRING
        " (Steam build " ZENGINE_STEAM_BUILD_ID ")");
}
```

---

## 12. Deliverables Checklist

### Build system core

- [ ] `CMakeLists.txt` (root) — project declaration, options, subdirectory inclusion, version configure_file
- [ ] `cmake/CompilerOptions.cmake` — `zengine_apply_compile_options()` function
- [ ] `cmake/Dependencies.cmake` — all FetchContent + find_package + vendored library targets
- [ ] `cmake/ShaderCompilation.cmake` — `zengine_compile_shaders()` function
- [ ] `cmake/Packaging.cmake` — CPack configuration for all platforms
- [ ] `cmake/toolchains/windows-msvc.cmake`
- [ ] `cmake/toolchains/linux-gcc.cmake`
- [ ] `cmake/toolchains/macos-clang.cmake`

### Target CMakeLists

- [ ] `ZEngine/CMakeLists.txt` — static library, all sources, platform conditionals
- [ ] `ZEditor/CMakeLists.txt` — editor executable, imgui, ImGuizmo, yaml-cpp
- [ ] `ZRuntime/CMakeLists.txt` — shipping runtime, Steam conditionals
- [ ] `ZGame/CMakeLists.txt` — game shared library
- [ ] `ZTests/CMakeLists.txt` — GTest integration, `gtest_discover_tests`
- [ ] `ZCook/CMakeLists.txt` — cook tool executable, msdf-atlas-gen dependency

### Version header

- [ ] `ZEngine/Core/EngineVersion.h.in` — template with `@PROJECT_VERSION_*@` substitutions

### Shader and asset pipeline

- [ ] `cmake/ShaderCompilation.cmake` — per-shader `add_custom_command`, incremental
- [ ] Root CMake — `CookAssets` custom target with stamp-based incremental logic

### Packaging

- [ ] Windows NSIS installer config + steam_api64.dll install rule
- [ ] Linux AppImage config (`cmake/AppImagePackage.cmake`)
- [ ] macOS `.app` bundle config + `scripts/sign_and_notarize.sh`

### CI/CD

- [ ] `.github/workflows/build.yml` — full matrix (windows-latest, ubuntu-24.04, macos-14) × (Debug, RelWithDebInfo, Release)
- [ ] Cache entries for CMake build dir and FetchContent
- [ ] Vulkan SDK install steps for all three platforms
- [ ] `ctest` invocation with `--output-on-failure`
- [ ] Package artifact upload (Release / main branch only)
- [ ] Sentry symbol upload step (Release / main branch only)

### Symbol management

- [ ] `scripts/upload_symbols_windows.sh` — PDB → Sentry
- [ ] `scripts/upload_symbols_linux.sh` — Breakpad `.sym` → Sentry
- [ ] `scripts/sign_and_notarize.sh` — macOS code sign + notarize + staple

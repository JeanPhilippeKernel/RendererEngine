# ZEngine — Build, Test, and Distribution Integration

**Status:** CMake presets, shader packaging, unit tests, and GitHub Actions
are implemented. A shipping packaging/cook pipeline remains design work.
**Tracked by:** [#510](https://github.com/JeanPhilippeKernel/RendererEngine/issues/510) for an
optional Ninja preset. Ninja is not part of the currently supported preset matrix.

## Current build topology

The repository root is the authoritative CMake entry point. It requires CMake
3.17, reads `VERSION.txt`, includes `dependencies.cmake`, and adds these
subdirectories:

```text
Resources   -> CompileShaders and packaged engine settings
ZEngine     -> zEngineLib static library and ZEngineTests
Tetragrama  -> Tetragrama editor library
Obelisk     -> executable linked with Tetragrama and zEngineLib
```

There are no current `ZGame`, `ZRuntime`, `ZCook`, or standalone
`ZEditor` targets. Those names must not be used as though they are buildable
today. `zEngineLib` is built as C++20; editor-only engine code is controlled
by `ZENGINE_BUILD_EDITOR`, which defaults to ON. `ZENGINE_TRACY` controls
the optional profiling integration.

`CMakePresets.json` is the supported configuration matrix:

| Platform | Debug preset | Release preset |
|---|---|---|
| Windows x64 | `Windows_x64_Debug_2022` / `_2026` | `Windows_x64_Release_2022` / `_2026` |
| macOS x64 | `Darwin_x64_Debug` | `Darwin_x64_Release` |
| macOS arm64 | `Darwin_arm64_Debug` | `Darwin_arm64_Release` |
| Linux x64 | `Linux_x64_Debug` | `Linux_x64_Release` |

For a local preset build, use the matching configure and build presets, for
example:

```sh
cmake --preset Darwin_arm64_Debug
cmake --build --preset Darwin_arm64_Debug
```

`Scripts/BuildEngine.ps1` is the repository automation path. It configures,
builds, and installs the selected Debug and/or Release presets, and may run
the clang-format script. It selects the platform-specific preset from its
configuration, architecture, and Visual Studio version arguments.

## Shader and test integration

`Resources/CMakeLists.txt` compiles `.vert`, `.frag`, and `.comp`
sources through `glslang-standalone` and copies SPIR-V to both the resource
cache and the installed engine package. `Obelisk` depends on
`CompileShaders`, so an explicit Obelisk build refreshes its shader package.
The resource target also copies the engine settings used at runtime.

`ZEngine/tests/CMakeLists.txt` builds `ZEngineTests`, registers discovered
GoogleTest tests when `BUILD_TESTING` is enabled, and installs the test
binary. It contains platform-specific crash-handler and runtime-path handling.

GitHub Actions run formatting and build/test workflows on Windows, macOS, and
Linux for source-tree changes. Documentation deployment is a separate MkDocs
workflow triggered only by `docs/**` or `mkdocs.yml` changes on `main`;
`ZEngine/docs/**` is not currently published by that workflow.

## Production completion criteria

1. Decide and document a supported CMake minimum and toolchain matrix from
   actual CI, rather than copying speculative compiler/version requirements.
2. Add first-class runtime/game, editor, and headless-cook targets only with a
   stable game/runtime boundary and install/package tests.
3. Make shader compilation dependency-complete for includes and fail packaging
   on absent or stale runtime SPIR-V.
4. Define reproducible artifact, signing, symbol, version, and release
   provenance policies. CPack/Steam distribution are not current build
   contracts.
5. Include the source documentation intended for publication in the docs build,
   or clearly maintain a separate internal-docs publication policy.

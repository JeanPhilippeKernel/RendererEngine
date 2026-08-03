# Shader Asset Pipeline

**Priority:** P3 — Implement alongside render-resource-manager.md  
**Status:** Design  
**Depends on:** `render-resource-manager.md`, `vfs-design.md` (Ticket 1)  
**Blocks:** `cook-pipeline.md` (SPIR-V artifacts), editor shader hot-reload

**Goal**: Implement a fully integrated shader asset pipeline inside `ZEngine::Rendering::Shaders`
that compiles GLSL source files to SPIR-V artifacts at import time, resolves `#include`
directives through the VFS, caches live `VkShaderModule` handles keyed by UUID, and
hot-reloads shaders — and their dependent pipelines — without stalling a frame in flight.
No exceptions. No `new`/`delete` in hot paths.

---

## 1. Shader Source Types and Stage Detection

### Supported source extensions

| Extension | Stage resolved by |
|-----------|------------------|
| `.vert`   | File extension — always `Vertex` |
| `.frag`   | File extension — always `Fragment` |
| `.comp`   | File extension — always `Compute` |
| `.glsl`   | `#pragma stage` directive in the file |

`.glsl` files are generic GLSL. Stage is resolved by scanning the first 64 lines of the
source for a `#pragma stage` directive before compilation begins. If no directive is
found, the importer records `ShaderStage::Unknown` and sets `ImportStatus::Failed`.

### `#pragma stage` directive syntax

```glsl
#pragma stage vertex
#pragma stage fragment
#pragma stage compute
```

The pragma value is case-insensitive. The directive may appear anywhere in the first
64 lines; lines are scanned in order and the first matching directive wins.

### `ShaderStage` enum

```cpp
// ZEngine/Rendering/Shaders/ShaderStage.h
#pragma once
#include <cstdint>

namespace ZEngine::Rendering::Shaders {

    enum class ShaderStage : uint8_t {
        Vertex   = 0,
        Fragment = 1,
        Compute  = 2,
        Unknown  = 0xFF
    };

    // Returns the ShaderStage implied by the file extension (lower-case extension
    // without the leading dot, e.g. "vert"). Returns Unknown for ".glsl" — caller
    // must inspect #pragma stage for those files.
    ShaderStage StageFromExtension(std::string_view ext);

    // Parses the first 64 lines of `source` for a "#pragma stage <value>"
    // directive. Returns Unknown if no directive is found.
    ShaderStage StageFromPragma(std::string_view source);

}  // namespace ZEngine::Rendering::Shaders
```

**Design notes**:

- `uint8_t` backing keeps `ShaderAsset` padding minimal; the field occupies one byte.
- `Unknown = 0xFF` is a deliberate non-zero sentinel so that a zero-initialised struct
  member is still detectable as uninitialised.
- `StageFromExtension` is called by `ShaderImporter` before reading file contents to
  fast-reject known-extension files without opening them when only the extension
  changes.
- `StageFromPragma` performs a line-by-line scan and stops at the first match. The 64-line
  cap prevents pathologically large files from stalling the import thread before the
  compiler even runs.

---

## 2. `ShaderAsset`

```cpp
// ZEngine/Rendering/Shaders/ShaderAsset.h
#pragma once
#include <uuid.h>
#include <Rendering/Shaders/ShaderStage.h>
#include <Core/ZEngineDef.h>

namespace ZEngine::Rendering::Shaders {

    struct ShaderAsset {
        uuids::uuid AssetUUID                          = {};
        ShaderStage Stage                              = ShaderStage::Unknown;
        char        ArtifactPath[MAX_FILE_PATH_COUNT]  = {};  // .spv file path on VFS
        char        EntryPoint[64]                     = "main";
        char        ErrorMessage[1024]                 = {};  // populated on compile failure
    };

}  // namespace ZEngine::Rendering::Shaders
```

**Design notes**:

- `ArtifactPath` is a VFS-relative path of the form
  `project://_cache/shaders/<uuid>.spv` (see Section 8). Storing it as a fixed-width
  `char` array avoids heap allocation and makes `ShaderAsset` trivially serialisable.
- `EntryPoint` defaults to `"main"` — Vulkan GLSL always uses `main`; the field exists
  to support future HLSL/MSL source where the entry point may differ.
- `ErrorMessage` is populated only when `ImportStatus::Failed` is set in the registry.
  On success it is left as a null-terminated empty string. The 1024-byte cap fits all
  shaderc single-error messages in practice; if shaderc produces more than one diagnostic,
  the importer concatenates them up to the buffer limit and appends `"..."`.
- `ShaderAsset` is a plain aggregate — no virtual destructor, no hidden pointers. It is
  safe to `memcpy` and to store in a flat file cache.
- `MAX_FILE_PATH_COUNT` is defined in `ZEngineDef.h` (currently 512). The same constant
  is used throughout the VFS layer.

---

## 3. `ShaderImporter` — Implements `IAssetImporter`

### Class declaration

```cpp
// ZEngine/Rendering/Shaders/ShaderImporter.h
#pragma once
#include <Assets/IAssetImporter.h>
#include <Rendering/Shaders/ShaderAsset.h>
#include <VFS/IVFSContext.h>

namespace ZEngine::Rendering::Shaders {

    class ShaderImporter final : public Assets::IAssetImporter {
    public:
        explicit ShaderImporter(VFS::IVFSContext& ctx);

        // IAssetImporter
        bool CanImport(const VFS::VFSPath& path) const override;
        // MUST NOT throw. ZEngine is compiled with -fno-exceptions.
        // Any exception thrown here calls std::terminate immediately.
        // All error paths must use VFSResult<T> or explicit error codes.
        Assets::ImportResult Import(const VFS::VFSPath& path,
                                    Assets::AssetRegistry& registry) override;

    private:
        VFS::IVFSContext& m_ctx;

        // Compiles GLSL source to SPIR-V. Returns true on success.
        // On failure, writes a null-terminated diagnostic into error_out.
        bool CompileWithShaderc(const VFS::VFSPath& source_path,
                                ShaderStage stage,
                                Core::Containers::Array<uint32_t>& spv_out,
                                char* error_out, uint32_t error_cap);

        // Subprocess fallback: invokes glslangValidator -V, reads the output .spv.
        bool CompileWithGlslang(const VFS::VFSPath& source_path,
                                ShaderStage stage,
                                Core::Containers::Array<uint32_t>& spv_out,
                                char* error_out, uint32_t error_cap);

        // Writes spv_words to ArtifactPath and returns true on success.
        // ShaderImporter::WriteArtifact uses the atomic write protocol:
        //   1. Write SPIR-V bytes to <uuid>.spv.tmp
        //   2. Flush and close
        //   3. Rename <uuid>.spv.tmp → <uuid>.spv
        // Ensures the SPIR-V file is never partially written. A partially-written SPIR-V
        // would cause vkCreateShaderModule to fail with VK_ERROR_INVALID_SHADER_NV or
        // similar, potentially crashing the engine at load time.
        bool WriteArtifact(const uuids::uuid& uuid,
                           const uint32_t* spv_words, uint32_t word_count);
    };

}  // namespace ZEngine::Rendering::Shaders
```

### Import flow

```
ShaderImporter::Import(path, registry)
  1. CanImport check (extension whitelist)
  2. Read existing .meta via MetaFileIO::Read(path)
     → If .meta exists and source hash unchanged: return ImportStatus::UpToDate
  3. Determine ShaderStage (StageFromExtension or StageFromPragma)
     → If Unknown: set ImportStatus::Failed, write ErrorMessage, return
  4. Attempt CompileWithShaderc(...)
     → On shaderc unavailable (link-time absent or runtime init failure):
       fall through to CompileWithGlslang(...)
  5. On compile failure: populate ErrorMessage, set ImportStatus::Failed in registry, return
  6. WriteArtifact(uuid, spv, word_count)
     → path: project://_cache/shaders/<uuid>.spv
  7. Populate ShaderAsset fields (UUID, Stage, ArtifactPath, EntryPoint)
  8. registry.Register(uuid, asset)
  9. MetaFileIO::Write(path, uuid, source_hash)
 10. Return ImportStatus::Success
```

### Compiler selection rationale

shaderc is the primary compiler: it is in-process, produces structured diagnostics,
and has no subprocess spawn cost. The glslangValidator fallback exists for build
configurations where linking shaderc is not desired (e.g., shipping builds that exclude
offline tools). The fallback is detected at compile time via `ZENGINE_HAS_SHADERC`
preprocessor define; if the define is absent, `CompileWithShaderc` is a no-op that
immediately returns `false`.

### Error capture

shaderc returns a `shaderc::SpvCompilationResult` whose `GetErrorMessage()` is a
`std::string`. The importer copies up to `error_cap - 1` bytes into `error_out` and
null-terminates. No allocation escapes the function.

The glslangValidator fallback captures `stderr` from the subprocess via a pipe; the
same truncation rule applies.

---

## 4. `VFSIncludeResolver`

shaderc resolves `#include` directives by calling back into a user-supplied
`IncluderInterface`. `VFSIncludeResolver` implements that interface by delegating
all path resolution to `IVFSContext`.

```cpp
// ZEngine/Rendering/Shaders/VFSIncludeResolver.h
#pragma once
#include <shaderc/shaderc.hpp>
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>

namespace ZEngine::Rendering::Shaders {

    class VFSIncludeResolver : public shaderc::CompileOptions::IncluderInterface {
    public:
        explicit VFSIncludeResolver(VFS::IVFSContext& ctx,
                                    const VFS::VFSPath& shader_dir);

        shaderc_include_result* GetInclude(const char* requested_source,
                                           shaderc_include_type type,
                                           const char* requesting_source,
                                           size_t include_depth) override;

        void ReleaseInclude(shaderc_include_result* data) override;

    private:
        VFS::IVFSContext& m_ctx;
        VFS::VFSPath      m_shader_dir;

        // Flat arena for shaderc_include_result + content storage.
        // Allocated from a per-compilation bump allocator; freed in ReleaseInclude.
        struct IncludeEntry {
            shaderc_include_result result;
            char                   source_name[MAX_FILE_PATH_COUNT];
            Core::Containers::Array<char> content;
        };
    };

}  // namespace ZEngine::Rendering::Shaders
```

### Resolution rules

1. **`include_depth` cap**: if `include_depth >= 16` return an error result with
   message `"include depth limit (16) exceeded"`. This prevents infinite include
   cycles without needing a visited-set.

2. **Absolute VFS paths** (`vfs://` prefix or leading `/`): pass directly to
   `m_ctx.OpenFile(path)`.

3. **Relative paths**: resolve against `m_shader_dir`. Construct a candidate path
   as `m_shader_dir / requested_source` and open via `m_ctx.OpenFile(candidate)`.

4. **File not found**: return an error result with message
   `"could not resolve include: <requested_source>"`. shaderc treats any non-null
   `error_message` as a compile error.

### Lifetime of `shaderc_include_result`

shaderc calls `ReleaseInclude` exactly once per `GetInclude` call (even on error).
Each `IncludeEntry` is heap-allocated with `new` inside `GetInclude` and deleted
inside `ReleaseInclude`. This allocation is off the hot render path (it occurs only
during shader import / recompilation, not during frame execution), so the single
`new`/`delete` pair is acceptable. No raw `new` escapes into game code.

### `shaderc_include_type` handling

- `shaderc_include_type_relative` (`#include "..."`) — resolve relative to
  `requesting_source`'s directory, not necessarily `m_shader_dir`.
  Extract the directory from `requesting_source` and prepend it.
- `shaderc_include_type_standard` (`#include <...>`) — resolve relative to
  `m_shader_dir` only. VFS has no system include search path.

---

## 5. `ShaderCache`

`ShaderCache` maps asset UUIDs to live `VkShaderModule` handles. It is the single
source of truth for which SPIR-V module is currently bound to a given shader UUID.
All access is guarded by a `std::shared_mutex` so render-thread reads (via `Lookup`)
do not block each other, while import-thread writes (via `Swap`/`Evict`) take an
exclusive lock.

```cpp
// ZEngine/Rendering/Shaders/ShaderCache.h
#pragma once
#include <uuid.h>
#include <vulkan/vulkan.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <shared_mutex>

namespace ZEngine::Rendering::Shaders {

    class ShaderCache {
    public:
        // Returns a VkShaderModule for the given UUID. If one does not exist,
        // creates it from the provided SPIR-V words and stores it.
        // spv and word_count must be valid if the UUID is not already cached.
        VkShaderModule GetOrCreate(const uuids::uuid& uuid,
                                   const uint32_t* spv,
                                   uint32_t word_count);

        // Atomically replaces the VkShaderModule for uuid with new_module.
        // The old module is pushed onto RenderResourceManager's per-frame
        // deletion queue so it is not destroyed while a frame referencing it
        // is still in flight.
        void Swap(const uuids::uuid& uuid, VkShaderModule new_module);

        // Removes the entry for uuid. The evicted VkShaderModule is pushed
        // onto RenderResourceManager's deletion queue.
        void Evict(const uuids::uuid& uuid);

        // Non-blocking read. Returns VK_NULL_HANDLE if uuid is not cached.
        VkShaderModule Lookup(const uuids::uuid& uuid) const;

    private:
        Core::Containers::UnorderedHashMap<uuids::uuid, VkShaderModule> m_modules;
        mutable std::shared_mutex m_mtx;
    };

}  // namespace ZEngine::Rendering::Shaders
```

### Module lifetime and deferred destruction

A `VkShaderModule` must remain alive for every frame that has recorded a command
buffer referencing it. Destroying it immediately on `Swap` or `Evict` would be a
use-after-free if a frame is still in flight.

Protocol:
1. `Swap` / `Evict` acquires an exclusive lock, removes the old handle from `m_modules`,
   then calls `RenderResourceManager::EnqueueDeletion(old_module)`.
2. `RenderResourceManager` defers destruction by `FRAMES_IN_FLIGHT` frames (typically 2
   or 3). The old `VkShaderModule` is destroyed only after the GPU has finished all
   commands that referenced it.
3. `GetOrCreate` holds a shared lock for the lookup phase. If the UUID is absent it
   releases the shared lock, then acquires an exclusive (unique) lock and re-checks the condition (double-checked locking pattern — required because C++ std::shared_mutex does not support lock upgrades), then calls
   `vkCreateShaderModule` and inserts the new handle.

### No `vkCreateShaderModule` in the hot path

// THREAD SAFETY: vkCreateShaderModule must be called on the render thread.
// ShaderCache::Swap does NOT call vkCreateShaderModule directly.
// Instead it posts a SwapRequest to RenderResourceManager::m_pending_swaps,
// which is consumed by RenderResourceManager::BeginFrame on the render thread.
// This ensures all Vulkan device operations are single-threaded.

`GetOrCreate` is called once during pipeline creation or hot-reload, not once per draw
call. `Lookup` (read-only, shared lock, single hash map probe) is the only path that
runs on the render thread during normal frame execution.

---

## 6. Hot-Reload Path

Hot-reload is event-driven. `FileWatcher` raises a file-modified event; downstream
components react in a fixed chain. No polling. No frame stalls (compilation runs on a
dedicated import thread).

### Event chain

```
FileWatcher::OnFileModified(vfs_path)
  │
  ▼
AssetRegistry::OnAssetModified(vfs_path)
  · Looks up the asset UUID associated with vfs_path
  · Sets ImportStatus::Pending for that UUID
  · Dispatches ShaderImporter::Import(vfs_path, registry) on the import thread
  │
  ▼
ShaderImporter::Import(vfs_path, registry)                 [import thread]
  · Recompiles source → new SPIR-V
  · On failure: writes ErrorMessage, sets ImportStatus::Failed
                logs diagnostic, stops — old module remains live
  · On success: writes new .spv to ArtifactPath
  │
  ▼
ShaderCache::Swap(uuid, new_module)                        [import thread]
  · Creates VkShaderModule from new SPIR-V
  · Replaces old handle; pushes old handle onto deletion queue
  │
  ▼
PipelineDependencyGraph::MarkShaderDirty(uuid)             [import thread]
  · Marks all pipelines that reference uuid as dirty
  │
  ▼
[Start of next frame — render thread]
PipelineDependencyGraph::CollectDirtyPipelines(out_dirty)
  · Returns all pipelines marked dirty since last ClearDirty
  │
  ▼
RenderResourceManager::RebuildPipelines(dirty_pipelines)
  · Fetches updated VkShaderModule via ShaderCache::Lookup
  · Destroys old VkPipeline (enqueued for deferred deletion)
  · Creates new VkPipeline
  · ClearDirty called after all pipelines are rebuilt
```

### Thread safety invariants

- `ShaderCache::Swap` and `MarkShaderDirty` are called from the import thread.
- `ShaderCache::Lookup` is called from the render thread.
- `CollectDirtyPipelines` and `ClearDirty` are called from the render thread at the
  start of a frame, before any draw calls. No import-thread writes overlap with
  frame-start reads because the render loop checks for dirty pipelines before
  recording commands.
- `PipelineDependencyGraph` is guarded by a separate `std::mutex` for its dirty-set
  writes; reads at frame-start acquire the same mutex.

### Compile failure policy

If recompilation fails, the old `VkShaderModule` and all dependent pipelines continue
to operate unchanged. `ImportStatus::Failed` is recorded in the registry and
`ErrorMessage` is populated so that editor tooling can display the diagnostic. No frame
is dropped. The user fixes the source and saves again; `FileWatcher` triggers a new
import cycle.

---

## 7. `PipelineDependencyGraph`

`PipelineDependencyGraph` maintains the bidirectional mapping between pipeline handles
and the shader UUIDs they depend on. It is the bridge between the shader hot-reload
event and the pipeline rebuild work at the start of the next frame.

```cpp
// ZEngine/Rendering/Shaders/PipelineDependencyGraph.h
#pragma once
#include <uuid.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Rendering/PipelineHandle.h>
#include <mutex>

namespace ZEngine::Rendering::Shaders {

    class PipelineDependencyGraph {
    public:
        // Records that `pipeline` depends on every UUID in `shader_uuids`.
        // Called once at pipeline creation time.
        void RegisterPipelineShaders(PipelineHandle pipeline,
                                     const Core::Containers::Array<uuids::uuid>& shader_uuids);

        // Marks all pipelines that reference `shader_uuid` as dirty.
        // Thread-safe; may be called from the import thread.
        void MarkShaderDirty(const uuids::uuid& shader_uuid);

        // Appends all currently dirty PipelineHandles to out_dirty (no duplicates).
        // Called from the render thread at frame start.
        void CollectDirtyPipelines(Core::Containers::Array<PipelineHandle>& out_dirty);

        // Clears the dirty set. Called after all dirty pipelines have been rebuilt.
        void ClearDirty();

        // Removes all forward and reverse edges for `pipeline`.
        // Called when a pipeline is destroyed.
        void UnregisterPipeline(PipelineHandle pipeline);

    private:
        // forward map: pipeline → shader UUIDs it uses
        Core::Containers::UnorderedHashMap<
            PipelineHandle,
            Core::Containers::Array<uuids::uuid>> m_pipeline_to_shaders;

        // reverse map: shader UUID → pipelines that use it
        Core::Containers::UnorderedHashMap<
            uuids::uuid,
            Core::Containers::Array<PipelineHandle>> m_shader_to_pipelines;

        // set of pipelines pending rebuild
        Core::Containers::Array<PipelineHandle> m_dirty;

        std::mutex m_mtx;
    };

}  // namespace ZEngine::Rendering::Shaders
```

### `RegisterPipelineShaders`

1. Acquire `m_mtx`.
2. Insert `(pipeline, shader_uuids)` into `m_pipeline_to_shaders`.
3. For each UUID in `shader_uuids`, append `pipeline` to `m_shader_to_pipelines[uuid]`.

### `MarkShaderDirty(uuid)`

1. Acquire `m_mtx`.
2. Look up `m_shader_to_pipelines[uuid]`. If absent, return.
3. For each `PipelineHandle` in the result: if it is not already in `m_dirty`, append it.
   (Linear scan of `m_dirty` is acceptable; dirty pipelines are rare and the set is small.)

### `UnregisterPipeline(pipeline)`

1. Acquire `m_mtx`.
2. Look up `m_pipeline_to_shaders[pipeline]`; for each UUID in its list, remove
   `pipeline` from `m_shader_to_pipelines[uuid]`.
3. Erase `m_pipeline_to_shaders[pipeline]`.
4. Remove `pipeline` from `m_dirty` if present.

### Dirty accumulation across multiple saves

If the user saves the same shader file twice before the next frame, `MarkShaderDirty`
is called twice. The duplicate guard in step 3 prevents the same pipeline from
appearing twice in `m_dirty`, so `RebuildPipelines` processes each pipeline exactly
once per frame.

---

## 8. Cook — Artifact Storage and Platform Table

### SPIR-V artifact path convention

All compiled SPIR-V artifacts are stored under the project's `_cache` virtual directory:

```
project://_cache/shaders/<uuid-string>.spv
```

The UUID string is the canonical hyphenated lowercase form produced by
`uuids::to_string(asset.AssetUUID)`, e.g.
`project://_cache/shaders/550e8400-e29b-41d4-a716-446655440000.spv`.

The `_cache` directory is:
- Excluded from version control (`.gitignore` entry managed by project scaffolding).
- Safe to delete in full; reimporting all shaders regenerates it.
- Flat (no subdirectories) to keep path construction O(1) and VFS enumeration fast.

### `.meta` file

Each source file `shaders/pbr.vert` on the VFS has a companion
`shaders/pbr.vert.meta` written by `MetaFileIO::Write`. The `.meta` stores:

| Field | Type | Purpose |
|-------|------|---------|
| `uuid` | `uuids::uuid` | Stable identity across reimports |
| `source_hash` | `uint64_t` | xxHash64 of source bytes at last import |
| `stage` | `ShaderStage` | Cached stage to skip pragma scan on re-open |
| `artifact_path` | `char[MAX_FILE_PATH_COUNT]` | VFS path of the .spv |

On reimport, `ShaderImporter` reads the `.meta`, recomputes `source_hash`, and skips
compilation if the hash is unchanged (`ImportStatus::UpToDate`). This makes incremental
builds O(1) per unchanged shader.

### UUID stability guarantee

The UUID written into `.meta` on first import is never changed by subsequent reimports
of the same source file. `ShaderCache` and `PipelineDependencyGraph` both use this UUID
as their key, so a clean rebuild (delete `_cache/`) followed by reimport produces the
same UUID from the `.meta` and all downstream references remain valid.

### Platform artifact table

| Platform | Format | Status | Toolchain |
|----------|--------|--------|-----------|
| Vulkan | SPIR-V (`.spv`) | Implemented | shaderc / glslangValidator |
| Metal | MSL (`.metal`) | Future | SPIRV-Cross (`spirv_msl.hpp`) |
| DirectX 12 | DXIL (`.dxil`) | Future | SPIRV-Cross + DXC |

The Metal and DXIL paths are reserved in `ShaderAsset::ArtifactPath` design: the field
is platform-agnostic. A future `MetalShaderImporter` and `DXILShaderImporter` will
follow the same `IAssetImporter` contract; `ShaderCache` will become a thin abstract
interface with platform-specific subclasses.

---

## 9. Unit Tests

File: `ZEngine/tests/Rendering/Shaders/ShaderPipelineTest.cpp`

### Test 1 — Compile `.vert` source produces valid SPIR-V in `ArtifactPath`

```cpp
TEST(ShaderImporter, CompileVertSourceProducesArtifact)
{
    // Arrange: mount an in-memory VFS with a minimal vertex shader
    InMemoryVFSContext ctx;
    ctx.WriteFile("shaders/test.vert", R"(
        #version 450
        void main() { gl_Position = vec4(0.0); }
    )");

    AssetRegistry registry;
    ShaderImporter importer(ctx);

    // Act
    auto result = importer.Import(VFSPath("shaders/test.vert"), registry);

    // Assert
    ASSERT_EQ(result, ImportStatus::Success);

    const ShaderAsset* asset = registry.GetAsset<ShaderAsset>(result.UUID);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->Stage, ShaderStage::Vertex);

    // ArtifactPath must be non-empty and the file must exist on the VFS
    EXPECT_STRNE(asset->ArtifactPath, "");
    EXPECT_TRUE(ctx.FileExists(VFSPath(asset->ArtifactPath)));

    // The .spv file must start with the SPIR-V magic number 0x07230203
    auto spv_bytes = ctx.ReadFile(VFSPath(asset->ArtifactPath));
    ASSERT_GE(spv_bytes.Size(), 4u);
    uint32_t magic = 0;
    std::memcpy(&magic, spv_bytes.Data(), sizeof(magic));
    EXPECT_EQ(magic, 0x07230203u);
}
```

### Test 2 — Syntax error sets `ImportStatus::Failed` and populates `ErrorMessage`

```cpp
TEST(ShaderImporter, SyntaxErrorSetsFailedAndErrorMessage)
{
    InMemoryVFSContext ctx;
    ctx.WriteFile("shaders/broken.frag", R"(
        #version 450
        void main() { THIS IS NOT GLSL }
    )");

    AssetRegistry registry;
    ShaderImporter importer(ctx);

    auto result = importer.Import(VFSPath("shaders/broken.frag"), registry);

    EXPECT_EQ(result, ImportStatus::Failed);

    const ShaderAsset* asset = registry.GetAsset<ShaderAsset>(result.UUID);
    ASSERT_NE(asset, nullptr);
    // ErrorMessage must be non-empty
    EXPECT_STRNE(asset->ErrorMessage, "");
    // ArtifactPath must NOT have been written (no stale .spv on failure)
    EXPECT_FALSE(ctx.FileExists(VFSPath(asset->ArtifactPath)));
}
```

### Test 3 — `#include` resolution through `VFSIncludeResolver`

```cpp
TEST(VFSIncludeResolver, IncludeResolvesFileOnVFS)
{
    InMemoryVFSContext ctx;
    ctx.WriteFile("shaders/common.glsl", R"(
        vec3 ToLinear(vec3 c) { return pow(c, vec3(2.2)); }
    )");
    ctx.WriteFile("shaders/pbr.frag", R"(
        #version 450
        #include "common.glsl"
        layout(location = 0) out vec4 outColor;
        void main() { outColor = vec4(ToLinear(vec3(1.0)), 1.0); }
    )");

    AssetRegistry registry;
    ShaderImporter importer(ctx);

    auto result = importer.Import(VFSPath("shaders/pbr.frag"), registry);

    // If VFSIncludeResolver fails to resolve common.glsl, compilation fails.
    ASSERT_EQ(result, ImportStatus::Success);

    const ShaderAsset* asset = registry.GetAsset<ShaderAsset>(result.UUID);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->Stage, ShaderStage::Fragment);
    EXPECT_STREQ(asset->ErrorMessage, "");
}
```

### Test 4 — Hot-reload swap: in-flight frame still sees old module; new frame sees new module

```cpp
TEST(ShaderCache, SwapPreservesOldModuleDuringFrameInFlight)
{
    MockRenderResourceManager rrm;  // records deferred deletions
    ShaderCache cache;

    const uuids::uuid uuid = uuids::uuid_system_generator{}();
    const uint32_t spv_v1[] = { 0x07230203, /* ... minimal valid spv ... */ };
    const uint32_t spv_v2[] = { 0x07230203, /* ... different spv ... */ };

    VkShaderModule mod_v1 = cache.GetOrCreate(uuid, spv_v1, ARRAY_COUNT(spv_v1));
    ASSERT_NE(mod_v1, VK_NULL_HANDLE);

    // Frame N begins — records mod_v1 into command buffer (simulated by storing it)
    VkShaderModule in_flight = cache.Lookup(uuid);
    EXPECT_EQ(in_flight, mod_v1);

    // Import thread swaps in v2
    VkShaderModule mod_v2 = MakeTestShaderModule(spv_v2, ARRAY_COUNT(spv_v2));
    cache.Swap(uuid, mod_v2);

    // mod_v1 must have been enqueued for deferred deletion, not destroyed yet
    EXPECT_TRUE(rrm.IsPendingDeletion(mod_v1));

    // New Lookup returns mod_v2
    EXPECT_EQ(cache.Lookup(uuid), mod_v2);

    // in_flight (mod_v1) is still a valid handle until rrm flushes it
    EXPECT_FALSE(rrm.IsDestroyed(mod_v1));
    rrm.FlushDeletionQueue(/*frames_elapsed=*/FRAMES_IN_FLIGHT);
    EXPECT_TRUE(rrm.IsDestroyed(mod_v1));
}
```

### Test 5 — `PipelineDependencyGraph`: `MarkShaderDirty` marks only dependent pipelines

```cpp
TEST(PipelineDependencyGraph, MarkDirtyOnlyAffectsDependentPipelines)
{
    PipelineDependencyGraph graph;

    PipelineHandle pipe_a{1};
    PipelineHandle pipe_b{2};
    PipelineHandle pipe_c{3};

    uuids::uuid shader_x = uuids::uuid_system_generator{}();
    uuids::uuid shader_y = uuids::uuid_system_generator{}();

    // pipe_a uses shader_x and shader_y
    graph.RegisterPipelineShaders(pipe_a, {shader_x, shader_y});
    // pipe_b uses shader_y only
    graph.RegisterPipelineShaders(pipe_b, {shader_y});
    // pipe_c uses neither
    graph.RegisterPipelineShaders(pipe_c, {});

    // Mark shader_x dirty
    graph.MarkShaderDirty(shader_x);

    Core::Containers::Array<PipelineHandle> dirty;
    graph.CollectDirtyPipelines(dirty);

    // Only pipe_a depends on shader_x
    ASSERT_EQ(dirty.Size(), 1u);
    EXPECT_EQ(dirty[0], pipe_a);

    graph.ClearDirty();

    // Now mark shader_y dirty — pipe_a and pipe_b should be dirty
    graph.MarkShaderDirty(shader_y);
    dirty.Clear();
    graph.CollectDirtyPipelines(dirty);

    ASSERT_EQ(dirty.Size(), 2u);
    EXPECT_TRUE(ContainsHandle(dirty, pipe_a));
    EXPECT_TRUE(ContainsHandle(dirty, pipe_b));
    EXPECT_FALSE(ContainsHandle(dirty, pipe_c));
}
```

### Test 6 — UUID stability: reimporting the same source preserves UUID from `.meta`

```cpp
TEST(ShaderImporter, ReimportPreservesUUIDFromMeta)
{
    InMemoryVFSContext ctx;
    ctx.WriteFile("shaders/stable.vert", R"(
        #version 450
        void main() { gl_Position = vec4(0.0); }
    )");

    AssetRegistry registry;
    ShaderImporter importer(ctx);

    // First import — UUID is assigned and written to .meta
    auto result1 = importer.Import(VFSPath("shaders/stable.vert"), registry);
    ASSERT_EQ(result1, ImportStatus::Success);
    uuids::uuid uuid_first = result1.UUID;
    EXPECT_FALSE(uuid_first.is_nil());

    // Modify the source (force a hash change so the importer does not skip)
    ctx.WriteFile("shaders/stable.vert", R"(
        #version 450
        void main() { gl_Position = vec4(1.0); }  // changed constant
    )");

    AssetRegistry registry2;
    auto result2 = importer.Import(VFSPath("shaders/stable.vert"), registry2);
    ASSERT_EQ(result2, ImportStatus::Success);

    // UUID must be the same — read from .meta, not regenerated
    EXPECT_EQ(result2.UUID, uuid_first);
}
```

---

## 10. Deliverables Checklist

### Core types
- [ ] `ZEngine/Rendering/Shaders/ShaderStage.h` — `ShaderStage` enum, `StageFromExtension`, `StageFromPragma`
- [ ] `ZEngine/Rendering/Shaders/ShaderAsset.h` — plain aggregate, fixed-width fields, no heap

### Import
- [ ] `ZEngine/Rendering/Shaders/ShaderImporter.h` + `.cpp` — implements `IAssetImporter`; shaderc primary, glslangValidator fallback; `ZENGINE_HAS_SHADERC` compile-time guard
- [ ] `ZEngine/Rendering/Shaders/VFSIncludeResolver.h` + `.cpp` — implements `shaderc::CompileOptions::IncluderInterface`; relative and absolute VFS paths; depth cap 16; proper `ReleaseInclude` cleanup

### Cache
- [ ] `ZEngine/Rendering/Shaders/ShaderCache.h` + `.cpp` — `GetOrCreate`, `Swap`, `Evict`, `Lookup`; `std::shared_mutex` for read/write separation; old modules pushed to `RenderResourceManager` deletion queue

### Hot reload
- [ ] `FileWatcher` → `AssetRegistry::OnAssetModified` → `ShaderImporter::Import` chain wired in engine startup
- [ ] `ShaderImporter::Import` runs on the dedicated import thread, not the render thread
- [ ] Compile failure leaves the current live module unchanged; `ImportStatus::Failed` + `ErrorMessage` recorded

### Dependency graph
- [ ] `ZEngine/Rendering/Shaders/PipelineDependencyGraph.h` + `.cpp` — forward + reverse adjacency lists; `MarkShaderDirty` thread-safe; `CollectDirtyPipelines` + `ClearDirty` called at frame start
- [ ] `RegisterPipelineShaders` called at pipeline creation; `UnregisterPipeline` called at pipeline destruction

### Cook / artifacts
- [ ] `.spv` artifacts written to `project://_cache/shaders/<uuid>.spv`
- [ ] `.meta` files written by `MetaFileIO::Write` on every successful import (UUID + source hash + stage)
- [ ] Incremental build: `source_hash` comparison skips recompilation when source is unchanged (`ImportStatus::UpToDate`)
- [ ] UUID read from existing `.meta` on reimport — never regenerated for an existing source file
- [ ] `_cache/` excluded from version control

### Tests
- [ ] `ZEngine/tests/Rendering/Shaders/ShaderPipelineTest.cpp` — all 6 tests pass under AddressSanitizer and UBSanitizer
- [ ] Manual smoke test: load a scene with 10 PBR pipelines, edit one `.frag` shader on disk, verify all dependent pipelines rebuild within the next frame with no validation layer errors and no GPU crash

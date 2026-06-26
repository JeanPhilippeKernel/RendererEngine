# Cook / Package Pipeline

**Priority:** P4 — Implement after import pipeline and asset registry are stable  
**Status:** Design  
**Depends on:** `import-pipeline.md`, `vfs-ticket6-asset-registry.md`, `render-resource-manager.md`  
**Blocks:** Shipping / release builds

**Goal**: Transform raw source assets (imported, meta-file-tracked) into platform-optimised,
pak-packaged artifacts that the runtime can mount without any further processing. The pipeline
is incremental (SHA256-gated), parallel where the dependency graph allows, and
headless-CI-compatible.

---

## 1. `CookTarget`

`CookTarget` is a pure data struct that describes *what* to cook and *where* to put it.
It carries no mutable state and is safe to copy across threads.

```cpp
// ZEngine/Cook/CookTarget.h
#pragma once
#include <Core/ZEngineDef.h>

namespace ZEngine::Cook
{
    enum class PlatformID : uint8_t
    {
        PC_Vulkan     = 0,
        Mobile_Vulkan = 1,
    };

    struct AssetFilter
    {
        bool IncludeMeshes    = true;
        bool IncludeMaterials = true;
        bool IncludeTextures  = true;
        bool IncludeShaders   = true;
        bool IncludeScenes    = true;
    };

    struct CookTarget
    {
        PlatformID  Platform;
        char        OutputPath[MAX_FILE_PATH_COUNT];
        AssetFilter Filter;
    };
}
```

**Design notes**:

- `PlatformID` is `uint8_t` so it can be stored in a manifest field without padding.
- `OutputPath` is a fixed-length C array — no heap allocation, safe to copy with `memcpy`.
- `AssetFilter` lets a developer cook only textures during a texture-tuning iteration,
  avoiding the cost of re-transcoding every mesh.
- `MAX_FILE_PATH_COUNT` is defined in `Core/ZEngineDef.h` (typically 512).

---

## 2. `CookManifest`

`CookManifest` is the incremental-cook ledger. It maps each asset UUID to its last-known
artifact record. When a cook run starts, the manifest is loaded from disk; at the end it is
saved back. Comparing the stored `SourceSHA256` against the current meta-file SHA is the sole
gate that decides whether an asset is re-cooked.

```cpp
// ZEngine/Cook/CookManifest.h
#pragma once
#include <uuid.h>
#include <Core/Containers/UnorderedHashMap.h>

namespace ZEngine::Cook
{
    struct ArtifactRecord
    {
        uint64_t Offset           = 0;
        uint32_t Size             = 0;
        char     SourceSHA256[65] = {};  // from MetaFileData::LastSourceSha256, null-terminated
    };

    class CookManifest
    {
    public:
        // Returns true when no record exists for uuid OR stored SHA differs from current_sha256.
        bool NeedsRecook(const uuids::uuid& uuid, const char* current_sha256) const;

        // Upserts (or inserts) a record for uuid.
        void Record(const uuids::uuid& uuid, ArtifactRecord record);

        // Marks uuid stale, then walks DependencyGraph to mark all transitive dependents stale.
        // "Stale" means the record is erased so NeedsRecook returns true on the next query.
        void MarkCascadeStale(const uuids::uuid& uuid);

        VFS::VFSResult<void> SaveToFile(VFS::IVFSContext& ctx, const VFS::VFSPath& path) const;
        VFS::VFSResult<void> LoadFromFile(VFS::IVFSContext& ctx, const VFS::VFSPath& path);

    private:
        Core::Containers::UnorderedHashMap<uuids::uuid, ArtifactRecord> m_records;
    };
}
```

**JSON schema** (written alongside the `.pak` file, e.g. `game.pak.manifest.json`):

```json
{
  "version": 1,
  "records": [
    {
      "uuid":   "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "offset": 4096,
      "size":   81920,
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    }
  ]
}
```

**Implementation notes**:

- `NeedsRecook`: look up `uuid` in `m_records`; if absent return `true`; if present, return
  `strncmp(record.SourceSHA256, current_sha256, 64) != 0`.
- `MarkCascadeStale`: erase the record for `uuid`, then call
  `DependencyGraph::CollectCascade(uuid)` to obtain all transitive dependents, and erase each
  of those records as well.
- `SaveToFile`: serialise `m_records` to JSON using `nlohmann::json`; write via
  `IVFSContext::OpenFile(path, Write | Create | Truncate)`.
- `LoadFromFile`: parse JSON with `allow_exceptions = false`; reject unknown `version`
  values; populate `m_records`; return `VFSError::InvalidData` on malformed input.
- The manifest file must be written *after* the pak is fully flushed to disk, so a crashed
  cook leaves an outdated manifest (conservative: next run re-cooks affected assets) rather
  than a stale one that skips needed work.

---

## 3. Incremental Cook

The incremental cook check runs per-asset, before any transcoding or pak-writing work begins.

```
For each asset uuid in cook order:
  1. Look up MetaFileData via MetaFileIO::Load(ctx, meta_path)
  2. current_sha = MetaFileData.LastSourceSha256
  3. if !manifest.NeedsRecook(uuid, current_sha):
       skip — artifact is already up to date
       continue
  4. Cook asset (import → transcode → stage)
  5. manifest.Record(uuid, {offset, size, current_sha})
```

**Cascade invalidation** happens as a pre-pass before the cook loop:

```
For each asset uuid whose source file has changed (SHA mismatch detected via a quick scan):
  manifest.MarkCascadeStale(uuid)
```

`MarkCascadeStale` calls `DependencyGraph::CollectCascade(uuid)` which performs a BFS/DFS
from `uuid` and returns every asset that directly or transitively depends on it. All of those
records are erased, so they will be re-cooked in the main loop even if their own source files
have not changed.

**Example**: texture T is changed. Material M references T. Mesh scene S references M.
`MarkCascadeStale(T)` erases records for T, M, and S. All three are re-cooked.

`DependencyGraph::CollectCascade` signature (for reference):

```cpp
// ZEngine/Assets/DependencyGraph.h  (existing)
Core::Containers::Array<uuids::uuid>
DependencyGraph::CollectCascade(const uuids::uuid& root) const;
```

---

## 4. Cook Order — Topological Sort

Assets must be cooked in dependency order: a material cannot be finalised before the textures
it references are cooked, and a scene cannot be packaged before its mesh/material assets are
ready.

The cook order is computed once per cook run, before the main loop.

```cpp
// ZEngine/Cook/CookOrder.h
#pragma once
#include <Assets/AssetRegistry.h>
#include <Assets/DependencyGraph.h>
#include <Cook/CookTarget.h>
#include <Core/Containers/Array.h>
#include <VFS/VFSResult.h>
#include <uuid.h>

namespace ZEngine::Cook
{
    class CookOrder
    {
    public:
        // Returns a topologically sorted list of asset UUIDs respecting the dependency graph.
        // Assets excluded by target.Filter are omitted from the result.
        // Returns VFSError::CyclicDependency if the graph contains a cycle.
        static VFS::VFSResult<Core::Containers::Array<uuids::uuid>>
        TopologicalSort(const AssetRegistry&    registry,
                        const DependencyGraph&  graph,
                        const CookTarget&       target);
    };
}
```

**Algorithm — Kahn's BFS** (no recursion, no stack overflow risk on deep dependency trees):

```
1. Build in-degree map: for each asset A, in_degree[A] = number of assets A depends on.
2. Seed queue with all assets where in_degree == 0 (no dependencies).
3. While queue not empty:
     u = dequeue
     append u to result
     for each v in graph.Dependents(u):   // v depends on u
         in_degree[v]--
         if in_degree[v] == 0:
             enqueue v
4. If result.Size() < total_asset_count: a cycle exists → return VFSError::CyclicDependency.
```

**Natural ordering by asset type** (textures before materials before meshes before scenes)
is a consequence of the dependency graph structure — textures have no dependencies, so they
all land at in-degree 0 and are processed first.

**Filtering**: assets excluded by `AssetFilter` are removed from the result after sorting,
but are still used during the sort to correctly compute in-degrees (so dependents of excluded
assets are not incorrectly treated as roots).

---

## 5. Texture Transcoding Pass

Raw source textures (RGBA8 from `IAssetImporter`) are transcoded to a GPU-native compressed
format before being written to the pak. This pass sits between import and pak-write.

```
Import → [RGBA8 pixels] → ITextureTranscoder → [compressed block data] → PakWriter
```

### `ITextureTranscoder`

```cpp
// ZEngine/Cook/ITextureTranscoder.h
#pragma once
#include <Core/Containers/Array.h>
#include <VFS/VFSResult.h>

namespace ZEngine::Cook
{
    class ITextureTranscoder
    {
    public:
        virtual ~ITextureTranscoder() = default;

        // Transcode RGBA8 source pixels to the implementation's compressed format.
        // out_compressed is appended to (not cleared); caller owns the array.
        virtual VFS::VFSResult<void> Transcode(
            const uint8_t*                      rgba8_src,
            uint32_t                            width,
            uint32_t                            height,
            Core::Containers::Array<uint8_t>&   out_compressed) = 0;

        // Human-readable format name used in log messages and manifest metadata.
        virtual const char* FormatName() const = 0;
    };
}
```

### `BC7TextureTranscoder` (PC_Vulkan)

```cpp
// ZEngine/Cook/BC7TextureTranscoder.h
#pragma once
#include <Cook/ITextureTranscoder.h>

namespace ZEngine::Cook
{
    // Uses bc7enc (https://github.com/richgel999/bc7enc_rdo) for BC7 compression.
    // BC7 is the preferred format for Vulkan on desktop; supported on all D3D11-class GPUs.
    class BC7TextureTranscoder final : public ITextureTranscoder
    {
    public:
        VFS::VFSResult<void> Transcode(
            const uint8_t* rgba8_src, uint32_t width, uint32_t height,
            Core::Containers::Array<uint8_t>& out_compressed) override;

        const char* FormatName() const override { return "BC7"; }
    };
}
```

**Implementation notes**:
- Call `bc7enc_compress_block` per 4x4 tile; results are 16 bytes per block.
- Width and height must be multiples of 4; assert in debug, return `VFSError::InvalidData` in
  release if not satisfied.
- Thread-safe: `bc7enc` compression is stateless per block.

### `ASTCTextureTranscoder` (Mobile_Vulkan)

```cpp
// ZEngine/Cook/ASTCTextureTranscoder.h
#pragma once
#include <Cook/ITextureTranscoder.h>

namespace ZEngine::Cook
{
    // Uses astcenc (https://github.com/ARM-software/astc-encoder) for ASTC 4x4 LDR.
    // ASTC is universally supported on mobile Vulkan (Android, Apple via MoltenVK).
    class ASTCTextureTranscoder final : public ITextureTranscoder
    {
    public:
        explicit ASTCTextureTranscoder(float quality = 60.0f);  // astcenc quality knob

        VFS::VFSResult<void> Transcode(
            const uint8_t* rgba8_src, uint32_t width, uint32_t height,
            Core::Containers::Array<uint8_t>& out_compressed) override;

        const char* FormatName() const override { return "ASTC_4x4_LDR"; }

    private:
        float m_quality;
    };
}
```

**Implementation notes**:
- Initialise `astcenc_context` once per coordinator thread; contexts are not shared.
- Block footprint: ASTC 4x4, 16 bytes per block — same block count as BC7.
- `quality` maps to `ASTC_PRE_FASTEST` (20) … `ASTC_PRE_THOROUGH` (100); 60 is a balanced
  default suitable for CI.

### Transcoder selection

`CookCoordinator` selects the transcoder based on `CookTarget::Platform`:

```cpp
Core::UniquePtr<ITextureTranscoder> MakeTranscoder(PlatformID platform)
{
    switch (platform)
    {
        case PlatformID::PC_Vulkan:     return Core::MakeUnique<BC7TextureTranscoder>();
        case PlatformID::Mobile_Vulkan: return Core::MakeUnique<ASTCTextureTranscoder>();
    }
    ZENGINE_ASSERT(false, "Unknown PlatformID");
    return nullptr;
}
```

---

## 6. Pak Format

The pak file is a single flat binary: fixed header, then a contiguous asset entry table,
then a raw data blob where all asset bytes are packed back-to-back, then the manifest JSON
appended at the end.

```
[PakHeader]           20 bytes
[AssetEntry × N]      32 bytes each
[Data blob]           variable (sum of all asset sizes, 16-byte aligned per entry)
[Manifest JSON]       variable (null-terminated UTF-8)
```

### `PakHeader`

```cpp
// ZEngine/Cook/PakFormat.h
#pragma once
#include <cstdint>

namespace ZEngine::Cook
{
    struct PakHeader
    {
        uint32_t Magic          = 0x5A504B47u;  // 'ZPKG' little-endian
        uint16_t Version        = 1;
        uint16_t Flags          = 0;            // reserved, must be 0
        uint32_t AssetCount     = 0;
        uint64_t ManifestOffset = 0;            // byte offset from file start to manifest JSON
    };
    static_assert(sizeof(PakHeader) == 20, "PakHeader size changed");

    struct AssetEntry
    {
        uint8_t  UUID[16];   // raw little-endian UUID bytes
        uint64_t Offset;     // byte offset from file start to asset data
        uint32_t Size;       // compressed size in bytes
        uint16_t Flags;      // bit 0 = LZ4-compressed, bit 1 = encrypted (reserved)
        uint16_t Reserved;   // must be 0
    };
    static_assert(sizeof(AssetEntry) == 32, "AssetEntry size changed");
}
```

### `PakWriter`

```cpp
// ZEngine/Cook/PakWriter.h
#pragma once
#include <Cook/PakFormat.h>
#include <Core/Containers/Array.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>
#include <VFS/VFSResult.h>
#include <uuid.h>

namespace ZEngine::Cook
{
    class PakWriter
    {
    public:
        // Stage one asset's cooked bytes. UUID and bytes are copied internally.
        void AddAsset(const uuids::uuid& uuid,
                      const Core::Containers::Array<uint8_t>& data,
                      uint16_t flags = 0);

        // Writes the complete pak to ctx at path.
        // Layout: PakHeader → AssetEntry[] → data blob → manifest JSON.
        // ManifestOffset in the header is patched before the final write.
        VFS::VFSResult<void> Flush(VFS::IVFSContext& ctx,
                                   const VFS::VFSPath& path,
                                   const CookManifest& manifest) const;

    private:
        struct StagedAsset
        {
            uuids::uuid                      UUID;
            Core::Containers::Array<uint8_t> Data;
            uint16_t                         Flags;
        };
        Core::Containers::Array<StagedAsset> m_staged;
    };
}
```

**`Flush` write sequence**:

// PakWriter::Flush uses the atomic write protocol:
//   1. Open <output_path>.tmp for Write | Create | Truncate
//   2. Write pak header
//   3. Write all asset data blocks
//   4. Write asset entry table
//   5. Flush OS write cache (fsync on POSIX, FlushFileBuffers on Windows)
//   6. Close the file
//   7. Rename <output_path>.tmp → <output_path>
//   8. Only AFTER rename: write CookManifest to <manifest_path>
// If the process crashes before step 7, the old pak (if any) is unchanged.
// If crash occurs between step 7 and step 8, the manifest is stale —
// next cook run detects SHA256 mismatch and re-cooks all assets.

1. Compute data offsets: `offset[0] = sizeof(PakHeader) + N * sizeof(AssetEntry)`;
   each subsequent offset = previous offset + `Align16(previous_size)`.
2. Build `AssetEntry[]` array with computed offsets.
3. Set `ManifestOffset = header_size + entries_size + total_data_size`.
4. Write `PakHeader`, then `AssetEntry[]`, then each asset's data (padded to 16 bytes),
   then the manifest JSON produced by `CookManifest::SaveToFile`.
5. All writes go through a single `IVFSContext::OpenFile(path, Write | Create | Truncate)` handle;
   no partial-write retry — the file is either complete or absent.

**No new/delete in the write path**: `Array<uint8_t>` uses the engine arena allocator;
`PakWriter` itself is stack-allocated by `CookCoordinator`.

---

## 7. `VFSPakBackend` as Runtime Pak Reader

**IMPORTANT FORMAT CLARIFICATION:** The `.pak` format is NOT a standard ZIP archive.
It uses a custom binary layout (`PakHeader { magic='ZPKG', ... }`) optimized for
sequential streaming reads. It is NOT mountable via `VFSZipBackend` (which uses
miniz and requires a valid ZIP EOCD structure).

Runtime mounting uses `VFSPakBackend` — a custom backend that reads the ZPKG format.
`VFSPakBackend` is specified separately from `VFSZipBackend`; they are NOT the same class.
This contradiction has been resolved — ZPKG is not a ZIP and requires its own backend.

The `.pak` file produced by the cook pipeline uses the custom ZPKG binary layout. `VFSPakBackend`
reads the ZPKG format at runtime without any ZIP compatibility layer.

**Asset path convention inside the pak**:

```
assets/<UUID-string>.<ext>
```

Example: `assets/a1b2c3d4-e5f6-7890-abcd-ef1234567890.tex`

**Runtime lookup flow**:

```
1. AssetRegistry::LookupByUUID(uuid)  →  AssetRecord { VFSPath artifact_path; ... }
2. IVFSContext::OpenFile(artifact_path)
   ↳ VFSPakBackend resolves "assets/<uuid>.tex" → entry in ZPKG asset table
   ↳ returns IVFSFile handle pointing at compressed/raw bytes
   (Note: VFSZipBackend reads true ZIP archives only; it does NOT read ZPKG .pak files.
    VFSPakBackend is the backend for cooked .pak files — see §7 for details.)
3. Read bytes → deserialise into runtime asset struct
```

No UUID-to-offset table is needed at runtime. The VFS path already encodes the location; the
ZIP central directory is the index. `AssetRegistry` holds the `VFSPath` that was recorded
during the cook, so the lookup is a single VFS open call.

**Mounting the pak at startup**:

```cpp
// In Application::Init or a platform bootstrap:
VFS::VFSPakBackend pakBackend;
pakBackend.Mount(ctx, VFSPath::Parse("build/pak/game.pak").Value(),
                 VFSPath::Parse("/pak").Value());
```

All asset paths in `AssetRegistry` are then resolvable under the `/pak` mount point.

---

## 8. `CookCoordinator`

`CookCoordinator` drives the full pipeline: load manifest, compute cook order, distribute
work across threads, flush the pak, save the manifest.

```cpp
// ZEngine/Cook/CookCoordinator.h
#pragma once
#include <Cook/CookManifest.h>
#include <Cook/CookTarget.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSResult.h>
#include <uuid.h>

namespace ZEngine::Cook
{
    class CookCoordinator
    {
    public:
        // Cook all assets described by target, updating manifest.
        // Thread count is read from Core::ThreadPoolHelper::WorkerCount().
        VFS::VFSResult<void> Cook(VFS::IVFSContext& ctx,
                                  const CookTarget&  target,
                                  CookManifest&      manifest);

    private:
        // Cook a single asset (import → transcode → stage into m_writer).
        // Called from worker threads; must be thread-safe.
        void CookAsset(const uuids::uuid& uuid,
                       const CookTarget&  target,
                       CookManifest&      manifest);

        PakWriter m_writer;
    };
}
```

**`Cook` orchestration steps**:

```
1. Load manifest from <OutputPath>/game.pak.manifest.json (ignore missing file → fresh cook).
2. Scan all source meta files; for each changed SHA, call manifest.MarkCascadeStale(uuid).
3. order = CookOrder::TopologicalSort(registry, graph, target)  →  fail fast on cycle.
4. Partition order into waves: each wave is a maximal set of assets with no intra-wave deps.
5. For each wave:
     Submit CookAsset(uuid) jobs to ThreadPoolHelper for all assets in wave.
     Wait for wave to complete before starting the next.
     Any job failure → abort remaining waves, return VFSError::CookFailed.
6. m_writer.Flush(ctx, OutputPath/game.pak, manifest).
7. manifest.SaveToFile(ctx, OutputPath/game.pak.manifest.json).
```

**Parallelism rules**:

- Assets in the same wave have no dependencies on each other — fully parallel.
- `CookAsset` accesses `PakWriter::AddAsset` which is guarded by a spinlock
  (writes to `m_staged` array).
- `CookManifest::Record` is guarded by a separate spinlock.
- `ImportCoordinator` and `IAssetImporter` implementations must be re-entrant; this is
  documented as a contract in `IAssetImporter.h`.

**CLI mode**:

```
ZEngineEditor --cook --platform pc --output build/pak/
ZEngineEditor --cook --platform mobile --output build/pak/mobile/ --filter textures,shaders
```

Exit codes:
- `0` — cook succeeded, pak written.
- `1` — cook failure (import error, transcode error, write error). Error details on stderr.
- `2` — missing dependency (an asset UUID referenced in `DependencyGraph` has no registered
  `IAssetImporter`). Indicates a misconfigured build, not a transient error.

Progress reporting: `CookCoordinator` emits `OnAssetCooked(uuid, elapsed_ms)` callbacks;
the CLI mode prints a progress bar to stdout using `\r` overwrite.

---

## 9. Editor Integration — Cook and Ship Buttons

The cook pipeline runs headless (no window, no GPU). It is invoked from:
- The editor's **[Cook]** toolbar button (from Tetragrama during development)
- The **[Ship]** wizard in Panzerfaust (automated Build → Cook → Package sequence)
- CI/CD pipeline (GitHub Actions, headless)

### 9.1 Cook button in Tetragrama

The `[Cook]` button in the editor toolbar runs the cook pipeline for the current
project platform and configuration. The editor streams cook progress to the log panel.

```
User clicks [Cook] in Tetragrama:
  1. Editor reads projectConfig.json to determine workingSpace and platform
  2. Editor spawns ZCook headlessly:
       ZCook --project path/projectConfig.json --platform PC_Vulkan --config Release
  3. ZCook runs CookCoordinator:
       - Reads cook.manifest (SHA256 gates)
       - Only re-cooks changed assets (incremental)
       - Packs output into CookedAssets/output.pak
  4. Editor streams ZCook's stdout to the log panel in real time
  5. On completion: "Cook complete — 47 assets, 3 re-cooked, output.pak (214 MB)"
  6. VFSPakBackend can now mount the new pak immediately for in-editor testing
```

**`ZCook` is a separate headless executable** (the `ZCook` CMake target from
`build-integration.md`). The editor does not embed the cook pipeline — it launches
ZCook as a child process. This keeps the editor process clean and allows the cook
to be killed/restarted without affecting the running editor.

### 9.2 Ship wizard in Panzerfaust

The Ship wizard sequences Build → Cook → Package in one flow. It lives in Panzerfaust
(not Tetragrama) because it produces the final distributable — a responsibility that
belongs to the project manager, not the editing environment.

```
User clicks [Ship] in Panzerfaust (or in Tetragrama toolbar shortcut):

  Step 1 — Build (Release)
    cmake --build Source/build --config Release --target ZEngineGame
    → produces MyGame.dll (Release, optimized, stripped)

  Step 2 — Cook
    ZCook --project projectConfig.json --platform PC_Vulkan --config Release
    → produces CookedAssets/output.pak

  Step 3 — Package
    cmake --build --target package
    → CPack produces:
         Windows:  MyGame_1.0_Setup.exe + MyGame_1.0_Win.zip
         Linux:    MyGame_1.0.AppImage
         macOS:    MyGame_1.0.dmg

  Optional Step 4 — Upload to Steam
    steamcmd +login ... +run_app_build app_build.vdf
    → uploads depot to Steam partner portal

Package contents (what the player installs):
  MyGame/
    bin/
      Obelisk.exe          ← engine entry point, no --launchEditor flag at runtime
      MyGame.dll           ← compiled game logic
    assets/
      output.pak           ← all cooked content
    (NO Panzerfaust, NO Tetragrama, NO source code, NO PluginSDK)
```

### 9.3 What is excluded from the shipping package

The shipping package is a GAME, not an engine SDK. The following are explicitly
excluded from all CPack output (enforced via `CPACK_IGNORE_FILES`):

```cmake
list(APPEND CPACK_IGNORE_FILES
    "/steam_appid\\.txt$"          # never ship this
    "/Panzerfaust"                 # launcher not needed by players
    "/Tetragrama"                  # editor not needed by players
    "/PluginSDK"                   # headers not needed by players
    "/Source/"                     # game source code not shipped
    "/Scripts/"                    # raw Lua not shipped (cooked into .pak)
    "/Cache/"                      # development metadata
    "cook\\.manifest$"             # development artifact
    "\\.pzf$"                      # project registry files
    "\\.spv$"                      # SPIR-V compiled into .pak
)
```

---

## 10. CI Integration Notes

The cook step runs headless — no window, no GPU, no editor UI. All GPU-format decisions
(BC7/ASTC) are made by software transcoders (`bc7enc`, `astcenc`) that run on CPU.

### GitHub Actions example

```yaml
# .github/workflows/cook.yml
name: Cook Assets

on:
  push:
    paths:
      - 'assets/**'
      - 'ZEngine/Cook/**'

jobs:
  cook-pc:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          lfs: true

      - name: Build ZEngineEditor (headless)
        run: cmake --build build --target ZEngineEditor --config Release

      - name: Cook PC pak
        run: |
          ./build/ZEngineEditor \
            --cook \
            --platform pc \
            --output build/pak/pc/
        # Exit code 1 or 2 automatically fails the job

      - name: Upload pak artifact
        uses: actions/upload-artifact@v4
        with:
          name: game-pak-pc
          path: build/pak/pc/game.pak

  cook-mobile:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          lfs: true

      - name: Build ZEngineEditor (headless)
        run: cmake --build build --target ZEngineEditor --config Release

      - name: Cook Mobile pak
        run: |
          ./build/ZEngineEditor \
            --cook \
            --platform mobile \
            --output build/pak/mobile/

      - name: Upload pak artifact
        uses: actions/upload-artifact@v4
        with:
          name: game-pak-mobile
          path: build/pak/mobile/game.pak
```

**Exit code summary**:

| Code | Meaning                                      | Action                          |
|------|----------------------------------------------|---------------------------------|
| `0`  | Cook succeeded                               | Upload artifact, proceed        |
| `1`  | Cook failure (import/transcode/write error)  | Fail CI, inspect stderr log     |
| `2`  | Missing dependency (no importer registered)  | Fail CI, fix asset registration |

**Incremental cook in CI**: cache `build/pak/` and `game.pak.manifest.json` between runs
(e.g. `actions/cache` keyed on `assets/` hash). On a cache hit the cook run completes in
seconds because only changed assets are re-cooked.

---

## 11. Unit Tests

File: `ZEngine/tests/Cook/CookPipelineTest.cpp`

### Test 1 — Incremental skip: matching SHA is not re-cooked

```cpp
TEST(CookManifest, SkipsAssetWithMatchingSHA)
{
    CookManifest manifest;
    uuids::uuid uuid = uuids::uuid_random_generator{}();

    ArtifactRecord rec;
    rec.Offset = 0; rec.Size = 512;
    strncpy(rec.SourceSHA256,
            "abc123abc123abc123abc123abc123abc123abc123abc123abc123abc123abc1",
            64);
    manifest.Record(uuid, rec);

    // Same SHA → no recook needed
    EXPECT_FALSE(manifest.NeedsRecook(uuid,
        "abc123abc123abc123abc123abc123abc123abc123abc123abc123abc123abc1"));
}
```

### Test 2 — SHA change triggers recook and cascade

```cpp
TEST(CookManifest, SHAChangeTriggersCascade)
{
    DependencyGraph graph;
    uuids::uuid texUUID  = uuids::uuid_random_generator{}();
    uuids::uuid matUUID  = uuids::uuid_random_generator{}();
    graph.AddDependency(matUUID, texUUID);  // mat depends on tex

    CookManifest manifest;
    ArtifactRecord rec; rec.Offset = 0; rec.Size = 1;
    strncpy(rec.SourceSHA256, "oldhash_padded_to_64_chars_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 64);
    manifest.Record(texUUID, rec);
    manifest.Record(matUUID, rec);

    manifest.MarkCascadeStale(texUUID);

    // Both tex and mat must now report NeedsRecook
    EXPECT_TRUE(manifest.NeedsRecook(texUUID, "newhash"));
    EXPECT_TRUE(manifest.NeedsRecook(matUUID, "oldhash_padded_to_64_chars_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
}
```

### Test 3 — TopologicalSort: texture appears before mesh

```cpp
TEST(CookOrder, TextureBeforeMesh)
{
    AssetRegistry registry;
    DependencyGraph graph;
    uuids::uuid tex  = uuids::uuid_random_generator{}();
    uuids::uuid mesh = uuids::uuid_random_generator{}();

    registry.Register(tex,  { AssetType::Texture });
    registry.Register(mesh, { AssetType::Mesh    });
    graph.AddDependency(mesh, tex);  // mesh depends on tex

    CookTarget target { PlatformID::PC_Vulkan };
    auto result = CookOrder::TopologicalSort(registry, graph, target);
    ASSERT_TRUE(result.IsOk());

    const auto& order = result.Value();
    ASSERT_EQ(order.Size(), 2u);

    auto texPos  = std::find(order.begin(), order.end(), tex);
    auto meshPos = std::find(order.begin(), order.end(), mesh);
    EXPECT_LT(texPos, meshPos);  // tex must come first
}
```

### Test 4 — PakWriter write + read: asset bytes round-trip correctly

```cpp
TEST(PakWriter, AssetBytesRoundTrip)
{
    MemoryVFSContext ctx;
    CookManifest manifest;

    Core::Containers::Array<uint8_t> src = {0xDE, 0xAD, 0xBE, 0xEF};
    uuids::uuid uuid = uuids::uuid_random_generator{}();

    PakWriter writer;
    writer.AddAsset(uuid, src);

    VFSPath pakPath = VFSPath::Parse("/test.pak").Value();
    ASSERT_TRUE(writer.Flush(ctx, pakPath, manifest).IsOk());

    // Re-open and validate header magic
    auto file = ctx.OpenFile(pakPath, VFS::Read);
    ASSERT_TRUE(file.IsOk());

    PakHeader header{};
    file.Value()->ReadAt(0, &header, sizeof(header));
    EXPECT_EQ(header.Magic, 0x5A504B47u);
    EXPECT_EQ(header.AssetCount, 1u);

    // Read AssetEntry and verify data bytes
    AssetEntry entry{};
    file.Value()->ReadAt(sizeof(PakHeader), &entry, sizeof(entry));

    Core::Containers::Array<uint8_t> dst(entry.Size);
    file.Value()->ReadAt(entry.Offset, dst.Data(), entry.Size);
    EXPECT_EQ(dst, src);
}
```

### Test 5 — Missing dependency returns descriptive error

```cpp
TEST(CookCoordinator, MissingImporterReturnsExitCode2)
{
    MemoryVFSContext ctx;
    AssetRegistry registry;
    DependencyGraph graph;

    uuids::uuid uuid = uuids::uuid_random_generator{}();
    registry.Register(uuid, { AssetType::Mesh });
    // Deliberately do NOT register an IAssetImporter for AssetType::Mesh

    CookTarget target { PlatformID::PC_Vulkan };
    CookManifest manifest;
    CookCoordinator coordinator;

    auto result = coordinator.Cook(ctx, target, manifest);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error(), VFSError::MissingDependency);
}
```

### Test 6 — CookManifest JSON round-trip preserves all ArtifactRecords

```cpp
TEST(CookManifest, JSONRoundTrip)
{
    MemoryVFSContext ctx;
    CookManifest original;

    for (int i = 0; i < 5; ++i)
    {
        uuids::uuid uuid = uuids::uuid_random_generator{}();
        ArtifactRecord rec;
        rec.Offset = static_cast<uint64_t>(i) * 4096;
        rec.Size   = 1024u + static_cast<uint32_t>(i);
        snprintf(rec.SourceSHA256, 65,
                 "%063d%d",  // 63 digits of i, then index
                 i, i);
        original.Record(uuid, rec);
    }

    VFSPath path = VFSPath::Parse("/manifest.json").Value();
    ASSERT_TRUE(original.SaveToFile(ctx, path).IsOk());

    CookManifest loaded;
    ASSERT_TRUE(loaded.LoadFromFile(ctx, path).IsOk());

    // Verify each UUID's record is identical after load
    // (Enumerate via a test-friend accessor or expose an iterator for test use)
    original.ForEachRecord([&](const uuids::uuid& uuid, const ArtifactRecord& rec) {
        EXPECT_FALSE(loaded.NeedsRecook(uuid, rec.SourceSHA256))
            << "Record mismatch after JSON round-trip";
    });
}
```

---

## 12. Deliverables Checklist

- [ ] `ZEngine/Cook/CookTarget.h`
- [ ] `ZEngine/Cook/CookManifest.h` + `CookManifest.cpp`
- [ ] `ZEngine/Cook/CookOrder.h` + `CookOrder.cpp` (Kahn's sort)
- [ ] `ZEngine/Cook/ITextureTranscoder.h`
- [ ] `ZEngine/Cook/BC7TextureTranscoder.h` + `.cpp` (bc7enc integration)
- [ ] `ZEngine/Cook/ASTCTextureTranscoder.h` + `.cpp` (astcenc integration)
- [ ] `ZEngine/Cook/PakFormat.h` (PakHeader + AssetEntry with static_asserts)
- [ ] `ZEngine/Cook/PakWriter.h` + `PakWriter.cpp`
- [ ] `ZEngine/Cook/CookCoordinator.h` + `CookCoordinator.cpp`
- [ ] CLI argument parsing: `--cook`, `--platform`, `--output`, `--filter`
- [ ] Exit codes `0` / `1` / `2` wired to `main()`
- [ ] `VFSPakBackend` mount verified with a cooked pak (integration smoke test)
- [ ] `tests/Cook/CookPipelineTest.cpp` (6 tests listed above)
- [ ] CI YAML workflow: `cook.yml` with separate `cook-pc` and `cook-mobile` jobs
- [ ] Manual smoke test: cook a scene with one texture → mesh → scene; confirm pak magic `ZPKG`;
      mount pak at runtime via `VFSPakBackend`; load scene; confirm entity names intact
- [ ] `ZCook` headless executable — invokable from editor, Panzerfaust, and CI
- [ ] Tetragrama `[Cook]` button — spawns ZCook subprocess, streams output to log panel
- [ ] Panzerfaust Ship wizard — sequences Build → Cook → Package → optional Steam upload
- [ ] `CPACK_IGNORE_FILES` list that excludes editor, PluginSDK, source, and raw scripts from shipping packages

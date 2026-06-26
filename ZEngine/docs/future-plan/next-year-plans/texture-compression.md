# ZEngine — Texture Compression (BCn / ASTC / KTX2)

**Priority:** Next-year plan — reduces VRAM usage 4–8x; required for shipping large games
**Status:** Design
**Depends on:** `cook-pipeline.md`, `render-resource-manager.md`, `vfs-ticket5-meta-uuid.md`

---

## 1. Why Compression

Uncompressed textures consume enormous VRAM. A single 4K RGBA8 albedo texture is 32MB. A 512-texture game with four texture maps per material (albedo, normal, roughness, emissive) at mixed 2K/4K resolutions carries roughly 8–12GB of raw texture data. Even a mid-range GPU with 8GB VRAM cannot hold all of it simultaneously.

GPU-native texture compression solves this at no runtime decompression cost. The GPU's texture units decompress on-the-fly in hardware — the sampler reads compressed texels and returns full-precision values to the shader as if the texture were uncompressed. The only costs are:

- Slightly lower quality compared to uncompressed (perceptually transparent at standard compression settings).
- Offline cook time to run the compressor.
- A requirement that texture dimensions be multiples of the block size (4x4 for BCn, variable for ASTC).

Compression ratios by format:

- BC7 (albedo, high quality): 8 bytes per 4x4 block = 4:1 over RGBA8.
- BC5 (normal maps, two-channel): 16 bytes per 4x4 block = 2:1 over RG8, 4:1 over RGBA8.
- BC4 (single-channel roughness/metallic): 8 bytes per 4x4 block = 8:1 over R8.
- BC6H (HDR/environment): 16 bytes per 4x4 block, half-float precision.
- ASTC 6x6 (general): ~2.37 bits per pixel, roughly 6:1 over RGBA8.

Applying compression across 500 textures reduces 10GB to 1.5–2GB — a 5–7x reduction that fits comfortably in 8GB VRAM alongside meshes, render targets, and buffers.

---

## 2. Format Selection by Platform and Texture Type

BCn is the standard for desktop Vulkan (Windows/Linux). ASTC is the Apple-preferred format for MoltenVK on macOS and is also available on modern Android. The cook pipeline generates the correct format per target platform.

| Texture type | Windows (Vulkan) | Linux (Vulkan) | macOS (MoltenVK) |
|---|---|---|---|
| Albedo (sRGB) | BC7 | BC7 | ASTC 6x6 |
| Normal map | BC5 | BC5 | ASTC 6x6 |
| Roughness/Metallic | BC4 (single-channel) | BC4 | ASTC 4x4 |
| HDR / environment | BC6H | BC6H | ASTC HDR 6x6 |
| Alpha-heavy (foliage) | BC3 | BC3 | ASTC 4x4 |

Format notes:

- **BC5** stores two channels (R and G). Normal maps are stored as two-channel (X, Y), with Z reconstructed in the shader as `sqrt(1 - x*x - y*y)`. This is the standard normal-map compression practice.
- **BC4** is single-channel. Roughness and metallic are stored in separate BC4 textures rather than packed into a combined BC3, because BC4 compresses a single channel more efficiently than BC3 compresses the same channel packed into a four-channel texture.
- **BC6H** is unsigned half-float. Environment maps and HDR light probes use this format; it preserves values above 1.0 which a standard BC7 cannot.
- **ASTC HDR** on macOS requires `MTLGPUFamilyApple6` or later. Fall back to RGBA16F (uncompressed) on older hardware.

The texture type is declared in the asset meta file (see section 5). The cook pipeline uses the meta `TextureType` field to select the format table row above.

---

## 3. Offline Compression Tools

Compression runs at cook time, not at runtime. Two tools are used:

**bc7enc** (MIT license):
- Compresses BC7, BC6H, BC5, BC4, BC3, BC1.
- Available as a single-header C library (`bc7enc.h`, `bc7enc.cpp`).
- Quality preset: `bc7enc_compress_block_params_init_slow` for final cook, `_fast` for iteration cooks.
- Located at `ZEngine/ThirdParty/bc7enc/` (cook-only, not compiled into the runtime).

**astcenc** (Apache 2.0 license):
- Compresses ASTC at all block sizes and HDR modes.
- Available as a library (`astcenc.h`) or CLI.
- Quality preset: `ASTCENC_PRE_MEDIUM` for final cook.
- Located at `ZEngine/ThirdParty/astcenc/` (cook-only).

Both tools are linked only into `ZEngine/Tools/Cook`. The runtime binary has no dependency on either.

The cook pipeline detects the target platform from the cook configuration and invokes the correct compressor. Cross-platform cooks (e.g. building a Windows pak on a macOS machine) are supported because both bc7enc and astcenc are pure CPU libraries with no platform-specific dependencies.

---

## 4. KTX2 Container Format

All compressed textures are stored in KTX2 (`.ktx2`). KTX2 is the Khronos standard container for GPU textures and is the preferred format for Vulkan deployments.

KTX2 capabilities used by ZEngine:

- **Multiple mip levels**: all mips are stored in a single KTX2 file, ordered from largest to smallest. This enables direct upload without separate mip generation at runtime.
- **Supercompression**: KTX2 supports zstd supercompression of the texture data within the container. This reduces on-disk and in-pak size by a further 10–30% for BCn textures (zstd compresses the block headers and repeated patterns well). The GPU receives decompressed BCn data — zstd decompression is done on the CPU during the load path.
- **Format metadata**: the KTX2 header contains `vkFormat` directly. The loader reads the header and creates the `VkImage` with the exact format without any runtime guessing.
- **Array textures and cube maps**: KTX2 supports texture arrays and cubemaps. Environment maps are stored as cubemap KTX2 files with `VK_IMAGE_VIEW_TYPE_CUBE`.

KTX2 reading at runtime uses **libktx** (Apache 2.0, Khronos), specifically the subset of libktx that handles KTX2 reading and supercompression decoding. The full libktx is not used — only the KTX2 read path. Located at `ZEngine/ThirdParty/libktx/` (runtime dependency, compiled into the engine).

The KTX2 file is the canonical on-disk representation of a compressed texture. The source PNG/TGA is not shipped.

---

## 5. TextureImporter

The `TextureImporter` is a new importer type registered in the import pipeline alongside `MeshImporter` and `MaterialImporter`.

```cpp
// ZEngine/Importers/TextureImporter.h

enum class TextureType : uint8_t {
    Albedo,       // sRGB, four-channel
    Normal,       // linear, two-channel (BC5 / ASTC)
    Roughness,    // linear, single-channel (BC4 / ASTC)
    Metallic,     // linear, single-channel (BC4 / ASTC)
    HDR,          // linear, half-float (BC6H / ASTC HDR)
    AlphaHeavy,   // sRGB with alpha (BC3 / ASTC for foliage, decals)
};

struct TextureImportConfig {
    TextureType Type        = TextureType::Albedo;
    bool        GenerateMips = true;
    uint32_t    MaxDimension = 4096;   // downscale if source exceeds this
};

class TextureImporter : public IImporter {
public:
    ImportResult Import(const VFSPath& source_path,
                        const TextureImportConfig& config,
                        const CookPlatform platform,
                        ArenaAllocator& arena) override;
};
```

Import steps:

1. Read source image (PNG, JPG, TGA, HDR) using `stb_image` (already a ZEngine dependency).
2. Validate dimensions: must be multiples of 4 (BCn requirement). Pad or downscale if needed.
3. Generate all mip levels in linear float space before compression (see section 8).
4. Select target `VkFormat` from the platform/type table in section 2.
5. Compress each mip level using bc7enc or astcenc.
6. Write output as KTX2 with zstd supercompression.
7. Return the KTX2 `VFSPath` and `AssetUUID` as the `ImportResult`.

The `TextureImportConfig` is stored in the asset meta file alongside the source texture:

```yaml
# example: assets/textures/rock_albedo.png.meta
UUID: "c7a3..."
TextureType: albedo
GenerateMips: true
MaxDimension: 2048
```

The cook pipeline reads the meta file and constructs `TextureImportConfig` before invoking `TextureImporter::Import`.

---

## 6. Runtime Loading

`RenderResourceManager::UploadTexture` is extended to accept a KTX2 source.

```cpp
// ZEngine/Rendering/RenderResourceManager.h (extension)

struct TextureUploadDesc {
    const void* KTX2Data    = nullptr;    // pointer to mapped KTX2 file bytes
    size_t      KTX2Size    = 0;
    bool        GenerateMips = false;     // false: all mips are in the KTX2 file
};

TextureHandle UploadTexture(const TextureUploadDesc& desc);
```

Upload path:

1. Parse KTX2 header using libktx to read `vkFormat`, dimensions, mip count, and supercompression type.
2. If supercompression is zstd: decompress the texture data block on the CPU into a staging buffer (allocated from the upload arena).
3. Create `VkImage` with the `vkFormat` from the header and `VK_IMAGE_TILING_OPTIMAL`.
4. For each mip level: record a `VkBufferImageCopy` from the staging buffer region to the image mip level.
5. Submit the copy commands in the upload command buffer.
6. Transition image layout to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
7. Return the `TextureHandle`.

No decompression of BCn or ASTC happens on the CPU or at runtime. The `VkBufferImageCopy` transfers the compressed block data directly into the image. The GPU texture unit handles decompression transparently.

The format of the uploaded `VkImage` exactly matches the `vkFormat` in the KTX2 header. The shader samples it with a standard `sampler2D` — no format-specific shader changes needed.

---

## 7. Platform Format Support Fallback

Not all platforms and GPU models support all formats. Particularly:

- BC7 requires `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` on `VK_FORMAT_BC7_UNORM_BLOCK`. Very old GPUs (pre-2012) may not support this.
- ASTC HDR requires `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` on `VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT`, which requires the `textureCompressionASTC_HDR` feature.
- MoltenVK on older macOS hardware may lack ASTC HDR support.

Format support is queried at renderer initialization via `vkGetPhysicalDeviceFormatProperties`:

```cpp
// ZEngine/Rendering/FormatSupport.h

struct FormatSupportTable {
    bool BC7_Supported       = false;
    bool BC6H_Supported      = false;
    bool BC5_Supported       = false;
    bool BC4_Supported       = false;
    bool BC3_Supported       = false;
    bool ASTC_LDR_Supported  = false;
    bool ASTC_HDR_Supported  = false;
};

FormatSupportTable QueryFormatSupport(VkPhysicalDevice physical_device);
```

`FormatSupportTable` is a scene-level singleton populated once at startup. The `TextureUploadDesc` path checks the table before creating the `VkImage`. If the required format is not supported, `UploadTexture` falls back to RGBA8 (uncompressed) by decompressing the BCn/ASTC data on the CPU using libktx's software decode path. A warning is logged:

```
[TextureImporter] WARNING: BC7 not supported on this GPU. Falling back to RGBA8 for asset "rock_albedo". Expected quality and VRAM usage will be higher.
```

The fallback path is never taken on a modern GPU. It exists to prevent hard crashes on edge-case hardware.

---

## 8. Mip Generation

Mip levels are generated offline, not at runtime. Offline mip generation for compressed textures is significantly higher quality than GPU-side generation because:

- The compressor can optimize each mip level independently with full knowledge of all texels.
- GPU mip generation (`vkCmdBlitImage`) applies a simple linear filter before compression; this can cause high-frequency artifacts in normal maps and roughness channels.
- Offline mips can use box filtering with gamma-correct sRGB conversions for albedo textures.

Mip generation in the cook pipeline:

1. Load source image at full resolution.
2. Generate all mip levels by halving dimensions iteratively, using a sinc (Lanczos-3) filter for albedo/HDR and a simple box filter for normal/roughness (to preserve derivative correctness).
3. Compress each mip level independently using bc7enc or astcenc.
4. Write all mips into the KTX2 file in order.

The result: the KTX2 file contains the complete mip chain, already compressed. `UploadTexture` uploads it level by level with no runtime mip generation.

If `GenerateMips = false` in `TextureImportConfig`, only mip level 0 is written to the KTX2. Used for render targets and textures that are known to render at fixed distance (UI elements, skybox).

---

## 9. Cook Pipeline Integration

Texture compression is a cook step executed by `CookCoordinator`. It runs before mesh and material cooks because material UUIDs reference texture UUIDs — the texture KTX2 UUID must be stable before the material asset is finalized.

```
Cook order (texture-relevant subset):
  1. TextureImporter::Import  -> produces .ktx2 with stable UUID
  2. MaterialImporter::Import -> references texture UUIDs from step 1
  3. MeshImporter::Import     -> references material UUIDs from step 2
```

Incremental cook (SHA256-gated):

- `CookCoordinator` stores a SHA256 hash of each source asset.
- Before invoking `TextureImporter::Import`, it checks whether the source PNG and meta file hashes match the stored hashes from the last successful cook.
- If both match: skip recompression and reuse the existing KTX2 from the pak. This avoids running bc7enc/astcenc (which can take several seconds per 4K texture) when the source has not changed.
- If either hash differs: recompress and update the stored hashes.

The SHA256 is computed from the source file bytes plus the `TextureImportConfig` bytes (so changing `TextureType` from `normal` to `albedo` forces recompression even if the PNG bytes are unchanged).

Cook is parallelized: `CookCoordinator` dispatches independent texture cook tasks to a thread pool. Up to `N-1` CPU cores run texture compression concurrently (where N is `std::thread::hardware_concurrency()`). Mesh and material cooks that depend on texture UUIDs wait on a completion barrier.

---

## 10. File Layout

```
ZEngine/Importers/
    TextureImporter.h           -- TextureImporter class, TextureImportConfig, TextureType
    TextureImporter.cpp         -- Import(): stb_image load, mip gen, bc7enc/astcenc dispatch,
                                   KTX2 write

ZEngine/Rendering/
    FormatSupport.h             -- FormatSupportTable struct, QueryFormatSupport()
    FormatSupport.cpp           -- vkGetPhysicalDeviceFormatProperties queries
    RenderResourceManager.h     -- TextureUploadDesc, UploadTexture() extended signature
    RenderResourceManager.cpp   -- KTX2 parse, staging buffer upload, VkImage creation

ZEngine/Tools/Cook/
    CookCoordinator.cpp         -- texture cook step, SHA256 gate, parallelized dispatch

ZEngine/ThirdParty/
    bc7enc/                     -- bc7enc.h, bc7enc.cpp (MIT, cook-only)
    astcenc/                    -- astcenc.h, astcenc_*.cpp (Apache 2.0, cook-only)
    libktx/                     -- KTX2 read/write (Apache 2.0, runtime + cook)
```

---

## 11. Deliverables Checklist

- [ ] `TextureType` enum and `TextureImportConfig` struct defined
- [ ] `TextureImporter::Import` implemented: stb_image load, dimension validation, mip generation
- [ ] bc7enc integrated and invoked for BCn formats (BC7, BC6H, BC5, BC4, BC3)
- [ ] astcenc integrated and invoked for ASTC formats (LDR and HDR)
- [ ] KTX2 output with zstd supercompression written via libktx
- [ ] Format selection table applied correctly per `TextureType` and target platform
- [ ] Asset meta schema extended with `TextureType`, `GenerateMips`, `MaxDimension` fields
- [ ] `FormatSupportTable` populated at startup via `vkGetPhysicalDeviceFormatProperties`
- [ ] RGBA8 fallback path in `UploadTexture` for unsupported compressed formats
- [ ] `UploadTexture` handles KTX2 source: header parse, zstd decompress, `VkBufferImageCopy` per mip
- [ ] Cook pipeline invokes `TextureImporter` before material cook
- [ ] SHA256-gated incremental recompression in `CookCoordinator`
- [ ] Cook parallelized across CPU cores (thread pool, texture cooks are independent tasks)
- [ ] Unit test: BC7 compressed output is valid KTX2 with correct `vkFormat` header
- [ ] Unit test: SHA256 gate skips recompression when source is unchanged
- [ ] Integration test: load scene with 50 compressed textures; assert VRAM consumption is within 4–8x of uncompressed baseline
- [ ] Visual validation: BC7 compressed albedo vs uncompressed — no visible blocking artifacts at standard viewing distance

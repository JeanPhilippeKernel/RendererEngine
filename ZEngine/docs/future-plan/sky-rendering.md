# Sky Rendering Systems

**Relates to:** `render-graph-integration.md`, `render-graph-redesign.md`, `gpu-allocator-rearchitecture.md`, `per-frame-upload-heap.md`
**Replaces:** `SkyboxPass` (currently in `RendererPasses.h`)
**Status:** Design
**Scope:** Four sky rendering backends and the `SkySystem` coordinator that activates exactly one backend per scene.

---

## 1. Motivation and Scope

The current `SkyboxPass` samples a `textureCube` bound to `set 0 binding 1`. It requires that the application always load a cube-map texture and provides no path for procedural atmospheres, image-based lighting capture, or lightweight gradient skies. Replacing it with a choice of four backends covers the full range of project needs from a simple editor background to a physically-based outdoor sky with aerial perspective.

| System | Mode value | Use case |
|---|---|---|
| `SkyAtmospherePass` | `SkyMode::Atmosphere` | Full physically-based outdoor sky |
| `SkyLightPass` | `SkyMode::Atmosphere` (auxiliary) | IBL capture from the active sky |
| `HDRIBackdropPass` | `SkyMode::HDRI` | Artist-supplied equirectangular HDR image |
| `SkySpherePass` | `SkyMode::SkySphere` | Lightweight gradient or texture-based sky |

`SkyLightPass` is not a standalone mode — it always runs alongside `SkyAtmospherePass` or `HDRIBackdropPass` to provide the diffuse irradiance and specular pre-filtered environment map consumed by the deferred lighting pass. The existing `SkyboxPass` struct, its vertex and fragment shaders (`skybox.vert`, `skybox.frag`), and the cube-map sampler binding at `set 0 binding 1` are removed once all four backends are validated.

---

## 2. SkyMode Enum and SkyConfig

```cpp
// ZEngine/Rendering/Sky/SkyConfig.h
enum class SkyMode : uint8_t
{
    Atmosphere = 0,  // SkyAtmospherePass + SkyLightPass
    HDRI       = 1,  // HDRIBackdropPass  + SkyLightPass
    SkySphere  = 2,  // SkySpherePass only; no IBL capture
};

struct SkyConfig
{
    SkyMode Mode = SkyMode::Atmosphere;

    // SkyAtmospherePass params
    Core::Maths::Vec4f RayleighScattering    = { 5.802e-6f, 13.558e-6f, 33.100e-6f, 0.0f };
    float              RayleighScaleHeight    = 8000.0f;
    float              MieScattering          = 3.996e-6f;
    float              MieAbsorption          = 4.400e-6f;
    float              MieScaleHeight         = 1200.0f;
    float              MieAnisotropy          = 0.8f;
    float              OzoneLayerCentre       = 25000.0f;
    float              OzoneLayerWidth        = 15000.0f;
    Core::Maths::Vec4f OzoneAbsorption        = { 0.650e-6f, 1.881e-6f, 0.085e-6f, 0.0f };
    float              PlanetRadiusKm         = 6360.0f;
    float              AtmosphereRadiusKm     = 6420.0f;
    float              SunAngularRadiusDeg    = 0.5334f;
    float              SunIlluminanceScale    = 10.0f;

    // HDRIBackdropPass params
    cstring            EnvironmentMapPath     = nullptr;  // VFS path to .hdr / .exr asset
    float              HDRIExposure           = 1.0f;
    Core::Maths::Vec4f HDRITint               = { 1.0f, 1.0f, 1.0f, 1.0f };

    // SkySpherePass params
    Core::Maths::Vec4f HorizonColor           = { 0.53f, 0.81f, 0.98f, 1.0f };
    Core::Maths::Vec4f ZenithColor            = { 0.10f, 0.31f, 0.72f, 1.0f };
    Core::Maths::Vec4f GroundColor            = { 0.30f, 0.27f, 0.24f, 1.0f };
    float              SunDiscSize            = 0.005f;
    float              SunDiscIntensity       = 5.0f;
    float              HorizonSharpness       = 8.0f;
    bool               ShowSunDisc            = true;
};
```

project.json extension:

```json
{
    "sky": {
        "mode": "atmosphere",
        "environmentMap": "$(workingSpace)/Assets/HDRI/outdoor_noon.hdr"
    }
}
```

When `mode` is `"atmosphere"` or `"skySphere"`, the `environmentMap` field is not required. When `mode` is `"hdri"`, a missing `environmentMap` causes `HDRIBackdropPass` to render solid black until an asset is assigned at runtime.

---

## 3. SkyAtmospherePass

### 3.1 Theory

Based on Bruneton 2017 and Hillaire 2020. Uses four pre-computed lookup tables rebuilt only when atmosphere parameters change. Per-frame cost is two full-screen triangle dispatches: one for the sky-view LUT and one to composite aerial perspective over scene geometry.

Scattering model: Rayleigh from air molecules (wavelength-dependent, isotropic phase function), Mie from aerosols (Henyey-Greenstein, asymmetry `g`), ozone absorption (Gaussian layer at 25 km). Sun direction derived from the scene's first active directional light.

### 3.2 LUT inventory

| LUT | Dimensions | Format | Rebuilt when | GpuMemoryDomain |
|---|---|---|---|---|
| Transmittance LUT | 256x64 | `R11G11B10_UFLOAT` | Atmosphere params change | `DeviceTexture` |
| Multiscattering LUT | 32x32 | `RGBA16F` | Atmosphere params change | `DeviceTexture` |
| Sky-view LUT | 192x108 | `RGBA16F` | Every frame | `RenderTarget` |
| Aerial perspective LUT | 32x32x32 | `RGBA16F` | Every frame | `RenderTarget` |

Transmittance and multiscattering LUTs are persistent — allocated once via `GpuAllocator::AllocateImage(DeviceTexture)`, uploaded via the staging ring when parameters change. Sky-view and aerial perspective are transient render targets declared as RenderGraph resources.

### 3.3 Atmosphere UBO

```cpp
// sizeof(SkyAtmosphereUBO) == 160 bytes, std140-compatible
// Pushed into PerFrameUploadHeap each frame via:
//   heap.Push(&atmo_ubo, sizeof(SkyAtmosphereUBO), device->MinUniformBufferOffsetAlignment())
struct SkyAtmosphereUBO
{
    Core::Maths::Vec4f RayleighScattering;     //   0
    Core::Maths::Vec4f MieScattering;          //  16
    Core::Maths::Vec4f MieAbsorption;          //  32
    Core::Maths::Vec4f OzoneAbsorption;        //  48
    float              PlanetRadius;           //  64
    float              AtmosphereRadius;       //  68
    float              RayleighScaleHeight;    //  72
    float              MieScaleHeight;         //  76
    float              MieAnisotropy;          //  80
    float              OzoneLayerCentre;       //  84
    float              OzoneLayerWidth;        //  88
    float              SunIlluminanceScale;    //  92
    Core::Maths::Vec4f SunDirection;           //  96
    float              SunAngularRadius;       // 112
    float              _pad[3];                // 116
    Core::Maths::Vec4f CameraPositionKm;       // 128
    float              _pad2[4];               // 144
};
// static_assert(sizeof(SkyAtmosphereUBO) == 160);
```

### 3.4 GLSL binding layout

```glsl
// set 0 binding 0 — camera UBO (dynamic offset, shared with all passes)
// set 0 binding 1 — SkyAtmosphereUBO (dynamic offset, PerFrameUploadHeap)

// LUT samplers (sky-view and combine passes)
layout(set = 1, binding = 0) uniform sampler2D  u_transmittance_lut;
layout(set = 1, binding = 1) uniform sampler2D  u_multiscatter_lut;
layout(set = 1, binding = 2) uniform sampler2D  u_skyview_lut;
layout(set = 1, binding = 3) uniform sampler3D  u_aerial_perspective_lut;

// LUT generation outputs (compute passes — storage images)
layout(set = 1, binding = 0, r11f_g11f_b10f) uniform writeonly image2D  o_transmittance;
layout(set = 1, binding = 0, rgba16f)          uniform writeonly image2D  o_multiscatter;
layout(set = 1, binding = 0, rgba16f)          uniform writeonly image2D  o_skyview;
layout(set = 1, binding = 0, rgba16f)          uniform writeonly image3D  o_aerial_persp;
```

Shader files:
- `sky_transmittance.comp` — local_size 8x8x1, 256x64 output, optical depth integration
- `sky_multiscatter.comp` — local_size 1x1x64, 32x32 output, first N scattering orders
- `sky_skyview.comp` — local_size 8x8x1, 192x108, non-linear lat-lon parameterisation (Hillaire 2020)
- `sky_aerial_perspective.comp` — local_size 8x8x1, 32x32x32, froxel in-scatter integration
- `sky_combine.frag` — composite sky over geometry; aerial perspective for scene pixels, sky-view for background pixels

### 3.5 Render graph integration

```
Setup():
    declare transient RenderTargets: "sky_view_lut" (192x108 RGBA16F),
                                     "aerial_persp" (32x32x32 RGBA16F)
    inputs:  hdr_depth (for aerial perspective compositing)
    outputs: hdr_lit (sky written on top), sky_view_lut, aerial_persp

Execute():
    1. if LUTsDirty:
           dispatch transmittance compute (32x8 groups)
           barrier: compute SHADER_WRITE -> SHADER_READ
           dispatch multiscatter compute
           barrier: compute SHADER_WRITE -> SHADER_READ
           LUTsDirty = false
    2. dispatch sky-view LUT compute (24x14 groups)
    3. dispatch aerial perspective LUT compute (4x4x8 groups)
    4. sky combine fullscreen draw(3,1,0,0) onto hdr_lit
```

### 3.6 Memory budget

| Resource | Format | Size | Domain |
|---|---|---|---|
| Transmittance LUT | R11G11B10_UFLOAT | 256x64 = 64 KB | `DeviceTexture` |
| Multiscattering LUT | RGBA16F | 32x32 = 8 KB | `DeviceTexture` |
| Sky-view LUT | RGBA16F | 192x108 = 162 KB | `RenderTarget` |
| Aerial perspective LUT | RGBA16F | 32x32x32 = 256 KB | `RenderTarget` |
| SkyAtmosphereUBO in heap | — | 160 B per frame-in-flight | `HostUniform` |
| **Total** | | **~490 KB** | |

---

## 4. SkyLightPass

### 4.1 Purpose

Captures sky radiance into three IBL resources for the deferred lighting pass:
- Diffuse irradiance cubemap — 32x32 per face, `RGBA16F`, 6 faces
- Specular pre-filtered env map — 128x128 base, 5 mip levels, `RGBA16F`, 6 faces
- BRDF integration LUT — 512x512, `RG16F` (split-sum `A` and `B` terms)

The BRDF LUT depends only on roughness and view angle. It is computed once at startup and never rebuilt. The diffuse and specular cubemaps are rebuilt on scene open, `SkyMode` change, and when `SkyAtmospherePass::MarkLUTsDirty()` is called.

### 4.2 Capture trigger

`SkySystem::m_ibl_dirty` is set in:
- `SkySystem::Initialize()` — always capture on first frame
- `SkySystem::SetConfig(cfg)` when mode changes
- `SkyAtmospherePass::MarkLUTsDirty()` — atmosphere params changed
- `HDRIBackdropPass::OnHDRIReady()` — new HDRI loaded

`SkyLightPass::Execute()` exits early if `!SkySystem::IsIBLDirty()`.

### 4.3 GLSL shader layout

```glsl
// Source sky input (cubemap from atmosphere or HDRI conversion)
layout(set = 0, binding = 0) uniform samplerCube u_sky_cubemap;

// IBL output storage images
layout(set = 0, binding = 1, rgba16f) uniform writeonly imageCube o_irradiance;
layout(set = 0, binding = 2, rgba16f) uniform writeonly imageCube o_prefiltered_mip;

// Push constants
layout(push_constant) uniform IBLPush {
    uint  FaceIndex;
    uint  MipLevel;
    float Roughness;
    uint  SampleCount;  // 1024 for prefiltered mip0, 64 for irradiance
} pc;
```

Shader files:
- `sky_brdf_lut.comp` — split-sum GGX A/B, 512x512 RG16F, local_size 8x8x1, run once at startup
- `sky_irradiance.comp` — cosine-weighted hemisphere integral, 64 samples, local_size 8x8x6, 32x32 cubemap output
- `sky_prefilter.comp` — GGX NDF importance sampling, 1024 samples at mip0, local_size 8x8x1

### 4.4 Integration with LightingPass

`SkyLightPass` exposes three getters. `LightingPass::Compile` calls them and binds to set 4:

```glsl
layout(set = 4, binding = 0) uniform samplerCube u_diffuse_irradiance;
layout(set = 4, binding = 1) uniform samplerCube u_specular_env_map;
layout(set = 4, binding = 2) uniform sampler2D   u_brdf_lut;
```

### 4.5 Memory budget

| Resource | Format | Size | Domain |
|---|---|---|---|
| Diffuse irradiance cubemap | RGBA16F | 32x32x6 = 48 KB | `DeviceTexture` |
| Specular env map cubemap | RGBA16F | 128x128x6 + 4 mips ≈ 1 MB | `DeviceTexture` |
| BRDF integration LUT | RG16F | 512x512 = 512 KB | `DeviceTexture` |
| **Total** | | **~1.5 MB** | |

---

## 5. HDRIBackdropPass

### 5.1 Pipeline

1. User places an `.hdr` or `.exr` in their project assets
2. Editor import pipeline (via `IAssetImporter`) converts to internal format and stores in `{project}/Assets/HDRI/`
3. `project.json` references the asset: `"sky": { "mode": "hdri", "environmentMap": "$(workingSpace)/Assets/HDRI/noon.hdr" }`
4. At scene load, `SkySystem::SetConfig` requests async load via the asset pipeline
5. On completion, `HDRIBackdropPass::OnHDRIReady(equirect_handle)` fires from the render thread
6. Next `Execute()` dispatches the equirect-to-cubemap compute
7. Every subsequent frame: fullscreen triangle backdrop draw only

When `EnvironmentMapPath` is null or asset fails to load: renders solid black background. No crash, no error log spam — only a single `ZENGINE_CORE_WARN` on first miss.

### 5.2 GLSL shader layout

**Equirect-to-cubemap compute** (`hdri_equirect_to_cube.comp`, local_size 8x8x6, dispatch 64x64x1 groups for 512x512 faces):

```glsl
layout(set = 0, binding = 0) uniform sampler2D   u_equirect;
layout(set = 0, binding = 1, rgba16f) uniform writeonly imageCube o_cubemap;
layout(push_constant) uniform EquirectPush { uint FaceSize; } pc;
// Converts cube face texel to direction, then maps to equirectangular UV via atan2/asin.
```

**Backdrop fullscreen** (`hdri_backdrop.frag`):

```glsl
layout(set = 0, binding = 0) uniform samplerCube u_hdri_cubemap;
// Camera UBO at set 0 binding 1 (dynamic offset)
layout(push_constant) uniform BackdropPush { float exposure; float tint[3]; } pc;
// Reconstructs world-space ray via inverse(mat4(mat3(View))) * inv_proj * clip.
// Samples cubemap along ray. Applies exposure and tint.
```

### 5.3 Memory budget

| Resource | Format | Size | Domain |
|---|---|---|---|
| Equirect source 4K | RGBA32F | 4096x2048 = 128 MB | `DeviceTexture` |
| Equirect source 2K | RGBA32F | 2048x1024 = 32 MB | `DeviceTexture` |
| Converted cubemap | RGBA16F | 512x512x6 = 6 MB | `DeviceTexture` |

Recommendation: import pipeline should offer optional downscale to 2K on import to reduce VRAM cost from 128 MB to 32 MB. If `GpuAllocator::HeapPressure` exceeds `WarnPressure (0.90)`, the import pipeline should trigger downscale automatically.

---

## 6. SkySpherePass

A lightweight fallback — fullscreen triangle with analytical gradient sky, optional sun disc, no LUTs, no cubemap. All parameters pass via push constants (80 bytes).

```cpp
struct SkySpherePush  // 80 bytes, within 128-byte guaranteed minimum
{
    float HorizonColor[4];
    float ZenithColor[4];
    float GroundColor[4];
    float SunDirection[4];  // w unused
    float SunDiscSize;
    float SunDiscIntensity;
    float HorizonSharpness;
    float ShowSunDisc;      // float bool
    float _pad;
};
```

**sky_sphere.frag** — trilinear gradient above/below horizon, Henyey-Greenstein-like sun disc via `acos(dot(dir, sun_dir)) < SunDiscSize`. Zero auxiliary GPU resources.

---

## 7. SkySystem

Coordinator. Owns all four pass instances. Activates exactly one backend per frame. Drives `SkyLightPass` when mode is `Atmosphere` or `HDRI`.

```cpp
struct SkySystem
{
    void Initialize(VulkanDevice*, RenderGraph*, ArenaAllocator*);
    void Update(const SkyConfig& cfg);      // call each frame from render thread
    void RegisterPasses(RenderGraph*);
    void Dispose();

    bool IsIBLDirty()   const;
    void ConsumeIBLDirty();
    void MarkIBLDirty();

    SkyLightPass* GetSkyLightPass();
    SkyMode       GetActiveMode() const;

private:
    SkyConfig         m_config;
    SkyMode           m_active_mode = SkyMode::Atmosphere;
    bool              m_ibl_dirty   = true;
    bool              m_mode_changed = false;

    SkyAtmospherePass m_atmosphere_pass;
    SkyLightPass      m_sky_light_pass;
    HDRIBackdropPass  m_hdri_pass;
    SkySpherePass     m_skysphere_pass;
};
```

`RegisterPasses` is called from `GraphicRenderer::RegisterPasses` in place of the old `SkyboxPass` registration:

```cpp
// Mode = Atmosphere or HDRI: register SkyLightPass first (IBL needed before LightingPass)
graph->AddCallbackPass("SkyLightPass", &m_sky_light_pass, mode != SkySphere);

switch (mode) {
    case Atmosphere: graph->AddCallbackPass("SkyAtmospherePass", &m_atmosphere_pass, true); break;
    case HDRI:       graph->AddCallbackPass("HDRIBackdropPass",  &m_hdri_pass,        true); break;
    case SkySphere:  graph->AddCallbackPass("SkySpherePass",     &m_skysphere_pass,   true); break;
}
graph->NodeMap["SkyboxPass"].Enabled = false;  // disable legacy pass
```

Mode changes at runtime require `RenderGraph::Compile()` — `SkySystem::Update` sets `m_mode_changed`; `GraphicRenderer::DrawScene` detects it and calls `m_render_graph->Compile()` before the next `Execute()`.

Sun direction is derived from the scene's first active directional light each frame in `SkySystem::Update`. Falls back to `normalize(1,1,0)` if no directional light exists.

---

## 8. Render Graph Pass Order

```
[DepthPrePass]         -> hdr_depth
[GeometryPass]         -> hdr_gbuffer
[SkyLightPass]         (conditional; rebuilds IBL cubemaps if dirty)
[LightingPass]         -> hdr_lit   (reads gbuffer + IBL from SkyLightPass)
[SkyAtmospherePass]    (or HDRIBackdropPass, or SkySpherePass)
    reads hdr_depth    (sky/geometry compositing)
    writes hdr_lit     (additive sky luminance)
[BloomThresholdPass]   -> bloom_threshold
[ToneMappingPass]      -> ldr_color
```

`SkyLightPass` declares no RenderGraph attachments (its IBL cubemaps are persistent externals). Its graph registration ensures `Setup` and `Execute` are called in the correct frame slot before `LightingPass` consumes the IBL handles.

---

## 9. Descriptor Set Assignment

| Set | Binding | Purpose |
|---|---|---|
| 0 | 0 | Camera UBO (UNIFORM_BUFFER_DYNAMIC, PerFrameUploadHeap) |
| 0 | 1..N | Pass-specific LUT samplers / cubemaps |
| 1 | 0 | `SkyAtmosphereUBO` (UNIFORM_BUFFER_DYNAMIC, PerFrameUploadHeap) |
| 2 | 0-3 | IBL outputs for LightingPass (diffuse irradiance, specular env map, BRDF LUT) |

Sky passes do not use the global bindless texture array. LUTs and cubemaps are explicit combined image samplers.

---

## 10. File Layout

```
ZEngine/Rendering/Sky/
├── SkyConfig.h
├── SkySystem.h / .cpp
├── SkyAtmospherePass.h / .cpp
├── SkyAtmosphereUBO.h
├── SkyLightPass.h / .cpp
├── HDRIBackdropPass.h / .cpp
└── SkySpherePass.h / .cpp

Resources/Shaders/Sky/
├── sky_transmittance.comp
├── sky_multiscatter.comp
├── sky_skyview.comp
├── sky_aerial_perspective.comp
├── sky_combine.vert / .frag
├── sky_irradiance.comp
├── sky_prefilter.comp
├── sky_brdf_lut.comp
├── hdri_equirect_to_cube.comp
├── hdri_backdrop.vert / .frag
├── sky_sphere.vert / .frag
```

---

## 11. Implementation Order

| Step | Deliverable | Depends on | Risk |
|---|---|---|---|
| 1 | `SkyConfig.h`, `SkyMode` enum | Nothing | None |
| 2 | `SkySpherePass` + `sky_sphere.frag` | Step 1 | Low — no LUTs, push constants only |
| 3 | `SkySystem` skeleton (mode switch, pass registration) | Steps 1-2 | Low |
| 4 | `SkyAtmospherePass` — transmittance + multiscatter LUT compute | Step 3, compute pipeline | Medium |
| 5 | `SkyAtmospherePass` — sky-view + aerial perspective per-frame LUTs | Step 4; requires `TextureSpecification.Depth` for 3D textures | Medium |
| 6 | `SkyAtmospherePass` — sky combine fullscreen graphics pass | Step 5 | Low |
| 7 | `SkyLightPass` — BRDF LUT (once at startup) | Step 4 | Low |
| 8 | `SkyLightPass` — irradiance + specular prefilter | Steps 6-7 | Medium |
| 9 | `SkyLightPass` integration with `LightingPass` set 4 | Step 8 | Medium |
| 10 | `HDRIBackdropPass` — equirect-to-cube compute | Steps 3, 8; `.hdr` importer | High |
| 11 | `HDRIBackdropPass` — backdrop fullscreen pass | Step 10 | Low |
| 12 | `HDRIBackdropPass` IBL source for `SkyLightPass` | Steps 9, 11 | Low |
| 13 | `SkySystem::Update` with mode change + dirty tracking | Steps 3-12 | Medium |
| 14 | Remove `SkyboxPass`, `skybox.vert/frag`, cube-map binding | All steps | Low |

Note: Step 5 requires `TextureSpecification` to support a `Depth` field and a `Type3D` image view type for the 32x32x32 aerial perspective LUT. This must be added before Step 5 can proceed.

---

## 12. Memory Budget Impact

| Mode | Resources | Total bytes | Domain |
|---|---|---|---|
| `Atmosphere` | 4 LUTs + 3 IBL textures | ~1.8 MB | DeviceTexture + RenderTarget |
| `HDRI` (2K source) | Equirect + cubemap + 3 IBL | ~39 MB | DeviceTexture |
| `HDRI` (4K source) | Equirect + cubemap + 3 IBL | ~135 MB | DeviceTexture |
| `SkySphere` | None | 0 | — |

For `Atmosphere` mode the budget impact is negligible. For `HDRI` mode with a 4K source, 135 MB is significant against the 512 MB `DeviceTexture` pool — the import pipeline should offer or auto-apply a 2K downscale option when heap pressure exceeds `WarnPressure (0.90)`.

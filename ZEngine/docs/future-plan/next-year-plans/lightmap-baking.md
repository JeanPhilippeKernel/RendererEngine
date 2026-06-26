# ZEngine — Lightmap Baking

**Priority:** Next-year plan — required for high-quality static lighting in indoor/architectural scenes
**Status:** Design
**Depends on:** `cook-pipeline.md`, `render-resource-manager.md`, `vfs-ticket5-meta-uuid.md`

---

## 1. What Lightmaps Solve

Dynamic lighting in real-time rendering has two fundamental costs: shadow computation and indirect lighting (global illumination, sky bounces, area light influence). For dynamic objects these costs must be paid every frame. For static geometry — walls, floors, structural props, terrain — the lighting from static light sources does not change and it is wasteful to recompute it every frame.

Lightmap baking is the offline precomputation of direct lighting, shadowing, and ambient occlusion for static geometry. The result is stored in a texture (the lightmap). At runtime, the fragment shader reads the lightmap instead of evaluating lights and shadows for static surfaces. The runtime cost of baked static lighting is one texture sample.

**What this enables:**
- Indoor scenes with rich soft shadows from area lights and ambient occlusion contact shadows
- Architectural visualisations with correct self-shadowing from complex geometry
- Large static environments where dynamic shadow rendering would be prohibitively expensive
- Zero per-frame CPU or GPU light evaluation cost for static portions of the scene

---

## 2. v1 Scope

**Included in v1:**

- Direct lighting from directional lights, point lights, and spot lights that are marked `LightComponent::BakingContribution = Static`
- Hard and soft shadow casting from static geometry (via BVH ray testing)
- Simple ambient occlusion via hemisphere ray sampling (64 samples per texel)
- Static geometry only: entities with `StaticMeshComponent` and `LightmapComponent`

**Not included in v1 (deferred to v2):**

- Indirect / bounce lighting (global illumination): requires path tracing with multiple bounces; significantly increases bake time and implementation complexity
- Emissive surface contribution: emissive meshes acting as area light sources
- Dynamic objects receiving lightmaps: dynamic objects that pass through a baked scene receive no lightmap contribution; they rely on dynamic lighting only
- Lightmap probe interpolation for dynamic objects (light probes): a separate system, tracked under `light-probes.md`
- GPU-accelerated baking (Vulkan compute or hardware ray tracing): v1 uses CPU path; GPU baking is a v2 upgrade

---

## 3. UV Unwrapping

Lightmap UVs are stored in a second UV channel, UV1 (UV0 is the albedo / material texture coordinate). UV1 maps each triangle of the mesh to a unique non-overlapping region of the [0,1]×[0,1] lightmap space. Overlapping UV1 UVs would cause baked lighting from one surface to bleed onto another.

**UV generation:** The cook pipeline generates UV1 automatically using `xatlas` (MIT license, automatic UV unwrapping for lightmaps). No manual UV1 authoring is required for v1.

Cook pipeline step:

1. Load mesh from source asset.
2. If mesh does not have UV1, run `xatlas::AddMesh` + `xatlas::Generate`.
3. Copy the generated `xatlas::Mesh::uv` array into the mesh asset as UV1 stream.
4. Write the updated mesh to the cooked asset cache.

`xatlas` parameters for ZEngine:

```cpp
xatlas::PackOptions pack_opts;
pack_opts.padding           = 2;   // 2-texel border between charts (prevents bleeding)
pack_opts.resolution        = 0;   // auto-determine atlas resolution from chart sizes
pack_opts.texelsPerUnit     = 16;  // lightmap resolution: 16 texels per world unit

xatlas::ChartOptions chart_opts;
chart_opts.maxIterations    = 4;
```

`texelsPerUnit` controls the baked lightmap quality. 16 texels per world unit is appropriate for architectural interiors. Outdoor terrain may use 4–8. This is a per-mesh setting stored in the cook manifest.

**UV1 storage:** Added as a second `VkFormat_R32G32_SFLOAT` vertex attribute at binding slot 1 in the mesh vertex buffer. The attribute is only present for static meshes that have `LightmapComponent`.

---

## 4. `LightmapComponent`

```cpp
struct LightmapComponent {
    uint32_t  LightmapHandle = UINT32_MAX;  // texture handle in RenderResourceManager
    Vec2f     UVOffset       = {};          // top-left of this mesh's lightmap rect in the atlas, [0,1]
    Vec2f     UVScale        = {};          // size of this mesh's lightmap rect in the atlas, [0,1]
};
```

`LightmapHandle` is a handle into `RenderResourceManager`'s texture pool. At scene load time, the cook pipeline output includes a single lightmap atlas texture per scene. `RenderResourceManager::LoadTexture` is called once for the atlas; all static meshes in the scene share the same atlas handle.

`UVOffset` and `UVScale` transform the mesh's UV1 coordinates (which are relative to the mesh's rect in the atlas) into the full atlas UV space:

```
atlas_uv = UVOffset + uv1 * UVScale
```

These values are written by the cook pipeline from the xatlas output.

`LightmapComponent` is a plain data component. No system depends on it for ticking; it is read by shaders via a per-entity UBO or push constant.

---

## 5. Lightmap Atlas

All static geometry in a scene shares one lightmap atlas. This minimises descriptor set changes during rendering — static meshes are batched and drawn with a single atlas texture bound.

**Default atlas dimensions:** 4096×4096. Configurable per-scene in the cook manifest (`lightmap_atlas_size: 4096`). 2048×2048 is available for scenes with fewer static objects or lower quality requirements.

**Chart packing:** xatlas packs all mesh UV1 charts into the atlas during the cook step. If the total chart area exceeds the atlas size, xatlas emits a warning and the cook fails. Resolution should be reduced (`texelsPerUnit`) or the scene should be split into multiple lightmap zones (v2).

**Atlas format (runtime):** BC6H (HDR block compression). Stores the baked irradiance as a half-float HDR texture, compressed to 1 byte per texel (vs 8 bytes for RGBA16F). At 4096×4096, BC6H costs 8 MB per atlas.

**Atlas format (intermediate / bake output):** RGBA16F uncompressed. BC6H compression is applied as a final cook step using the ZEngine texture compression pipeline (`texture-compression.md`).

---

## 6. Baking Process

Baking runs as a headless cook step, invoked by the `CookCoordinator` when scene geometry or light configuration changes (see §8). It does not run at engine startup during gameplay.

**Baking backend:** Intel Embree (Apache 2.0 license) for BVH construction and ray traversal. Embree provides a highly optimised CPU ray tracing API. The bake step links against `embree4` as a build-time dependency (not a runtime dependency — the baked output is stored in the cook cache).

**Baking algorithm per lightmap texel:**

```
For each static mesh M in the scene:
    For each lightmap texel T in M's atlas rect:
        Compute world position P and surface normal N for T
            (bilinear interpolation from mesh vertices using UV1)
        Accumulate direct irradiance:
            For each static light L:
                Compute L's contribution at P with normal N
                    (standard point/spot/directional attenuation)
                Cast shadow ray from P toward L
                If ray reaches L without obstruction: add contribution
        Accumulate ambient occlusion:
            For 64 cosine-weighted random directions in the hemisphere at N:
                Cast AO ray from P in direction D, max distance = AO_MAX_DIST (default: 2m)
                If ray hits geometry: ao_occlusion++
            ao_factor = 1.0 - (ao_occlusion / 64)
        Write texel: irradiance_rgb * ao_factor
```

**Output:** `<scene_name>.lightmap.r16g16b16a16f` (raw HDR atlas). The cook pipeline then passes this to the BC6H compression step.

**Parallelism:** Texel evaluation is embarrassingly parallel. The bake step uses `std::thread` (or a ZEngine job system if one exists at bake time) to distribute work across CPU cores.

**Thread safety:** Baking threads must write to strictly non-overlapping regions of the
output atlas buffer. Dividing by texel row is NOT safe because a single mesh's UV island
can span multiple rows — two threads could write overlapping regions.

Correct partitioning: divide by MESH, not by row. Assign each thread a disjoint subset
of meshes. Since each mesh's UV island occupies a unique non-overlapping atlas region
(guaranteed by xatlas), per-mesh assignments produce non-overlapping writes with no
synchronization required.

```
Thread 0: meshes [0,   N/4)
Thread 1: meshes [N/4, N/2)
Thread 2: meshes [N/2, 3N/4)
Thread 3: meshes [3N/4, N)
```

No mutex needed — each thread writes to its own disjoint atlas sub-regions.
After all threads complete, merge the per-mesh regions into the final atlas
(this is a memcpy of non-overlapping regions, also parallelizable).

Bake time for a 4096×4096 atlas with 100 static meshes and 10 lights: approximately 30–120 seconds depending on scene complexity and core count.

---

## 7. Runtime Usage

**GeometryPass (forward path):** For static meshes with `LightmapComponent`, the fragment shader samples the lightmap atlas at the atlas UV (UV1 transformed by UVOffset/UVScale). The lightmap irradiance is multiplied into the diffuse term in place of the dynamic diffuse light computation. The dynamic directional light contribution is suppressed for static meshes (their direct lighting is already baked).

```glsl
// In geometry/forward lighting fragment shader
#ifdef HAS_LIGHTMAP
    vec2 atlas_uv  = u_lm_offset + v_uv1 * u_lm_scale;
    vec3 lm_irrad  = texture(u_lightmap_atlas, atlas_uv).rgb;
    diffuse = lm_irrad;  // replace dynamic diffuse with baked
#else
    diffuse = compute_dynamic_diffuse(normal, position);
#endif
```

`u_lm_offset` and `u_lm_scale` are push constants set from `LightmapComponent::UVOffset` and `LightmapComponent::UVScale` by the render system.

**DeferredLightingPass (deferred path):** For static meshes, the lightmap atlas UV is packed into `GBufferPass`'s RT2 reserved BA bytes. The deferred lighting shader reads the lightmap and suppresses dynamic light evaluation for static surfaces:

```glsl
// In deferred_lighting.frag.glsl
vec2 lm_uv = unpack_lightmap_uv(texture(u_gbuffer_metallic_emissive, screen_uv));
if (lm_uv.x >= 0.0) {  // valid lightmap UV (invalid = -1,-1)
    vec3 lm = texture(u_lightmap_atlas, lm_uv).rgb;
    out_color = evaluate_static_pbr(g, lm);
} else {
    out_color = evaluate_dynamic_pbr(g, light_grid, light_index_list);
}
```

---

## 8. Invalidation

Lightmaps must be rebaked when the baked lighting conditions change. Stale lightmaps are incorrect but not crashes — they simply display outdated lighting. The cook pipeline enforces freshness via SHA256 tracking.

**Invalidation conditions:**

| Condition | Detection |
|---|---|
| A static mesh's transform changed | TransformComponent dirty flag + cook manifest SHA256 of transform list |
| A static light's position, intensity, or color changed | LightComponent serialised value SHA256 in cook manifest |
| The set of static meshes in the scene changed | SHA256 of entity list in cook manifest |
| Any static mesh geometry was modified (reimported) | SHA256 of the cooked mesh asset |

**Detection mechanism:** The cook pipeline maintains a `lightmap_manifest.json` per scene. On cook, the manifest records SHA256 hashes of all inputs (mesh transforms, light parameters, mesh asset hashes). On the next cook run, if any input hash has changed, the lightmap is marked invalid and the bake step is queued. If no inputs changed, the existing baked atlas is used as-is.

**Editor workflow:** The editor marks lightmaps invalid and displays an "Outdated lightmap" warning in the scene inspector. The cook step is triggered manually ("Bake Lightmaps" button) or automatically on cook-before-play if enabled.

---

## 9. Integration with Deferred Rendering

The interaction between lightmaps and the deferred rendering path requires careful G-buffer design because the deferred lighting shader needs to know which pixels have baked lighting and what their lightmap UV is.

**Packing strategy:** The reserved BA bytes in G-buffer RT2 (`VK_FORMAT_R8G8B8A8_UNORM`) are used to store the lightmap UV, packed as two 8-bit normalised values. This gives 256 discrete steps per axis across the atlas — sufficient for 4096×4096 atlas lookups (4096 / 256 = 16 texels of resolution loss, acceptable).

If 8-bit resolution proves insufficient (visible banding at close range), the packing can be upgraded to a `VK_FORMAT_R16G16_UNORM` auxiliary G-buffer target at the cost of 8 MB at 4K.

**Static vs dynamic flag:** A UV of (-1, -1) (clamped to 0,0 in 8-bit, so a reserved sentinel value is needed) indicates a dynamic mesh. Alternative: pack a 1-bit `is_static` flag into the metallic channel's highest bit.

**Forward path:** No changes to the forward G-buffer packing are needed. Lightmap UV is passed via push constants and the forward shader reads UV1 directly from the vertex input.

---

## 10. File Layout

```
ZEngine/Rendering/Lightmap/
    LightmapComponent.h
    LightmapSystem.h            -- (minimal) handles atlas texture binding per draw
    LightmapSystem.cpp
    Shaders/
        lightmap_sample.glsl    -- shared lightmap UV sampling helper (included by geometry shaders)

ZEngine/Cook/
    LightmapBaker.h
    LightmapBaker.cpp           -- Embree-based baking; texel irradiance loop
    LightmapUVUnwrap.h
    LightmapUVUnwrap.cpp        -- xatlas wrapper; UV1 generation and cook manifest write
    LightmapManifest.h
    LightmapManifest.cpp        -- SHA256 tracking, invalidation detection
```

`CookCoordinator` gains a `LightmapBakeStep` entry point called when the lightmap manifest is invalid.

Third-party dependencies added to `ZEngine/ThirdParty/`:
- `xatlas/` — MIT license; header-only + single translation unit
- `embree4/` — Apache 2.0; prebuilt library for macOS (arm64 + x86_64), Windows (x64), Linux (x64)

Both dependencies are cook-time only. They are not linked into the runtime engine binary (`ZENGINE_COOK` CMake target only).

---

## 11. Deliverables Checklist

- [ ] `LightmapComponent.h` — ECS component with handle, UVOffset, UVScale
- [ ] xatlas integration in `LightmapUVUnwrap.cpp` — UV1 generation from mesh data
- [ ] `LightmapBaker.cpp` — Embree scene construction, per-texel irradiance + AO sampling, RGBA16F output
- [ ] `LightmapManifest.cpp` — SHA256 input tracking, invalidation detection, manifest read/write
- [ ] `CookCoordinator` bake step integration — `LightmapBakeStep` queued on manifest invalidation
- [ ] BC6H compression applied to baked atlas output (via existing texture compression pipeline)
- [ ] Forward path shader change: lightmap sample replaces dynamic diffuse for `HAS_LIGHTMAP` variant
- [ ] Deferred path: lightmap UV packing into RT2 BA in `GBufferPass`; unpack + sample in `DeferredLightingPass`
- [ ] `memory-budget.md` updated: BC6H 4096×4096 atlas = 8 MB per scene
- [ ] Lightmap baking: thread work divided by mesh, not by row — no shared atlas writes
- [ ] Unit test: two threads baking non-overlapping meshes; verify no data races under TSAN
- [ ] Cook test: bake a simple box room scene with one point light; verify shadow boundary correctness in RenderDoc
- [ ] Invalidation test: move a static mesh, cook again, verify lightmap is re-baked
- [ ] Visual test: compare runtime forward-path lightmapped render against reference offline render
- [ ] Deferred path visual test: same scene in deferred mode, verify lightmap sampling agrees with forward path result

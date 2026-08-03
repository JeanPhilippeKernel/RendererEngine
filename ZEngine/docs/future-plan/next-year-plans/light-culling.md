# ZEngine — Tile-Based Light Culling

**Priority:** Next-year plan — required for scenes with more than ~20 dynamic lights
**Status:** Design
**Depends on:** `render-graph-integration.md`, `shadows.md`, `actor-ecs-architecture.md` (LightComponent)

---

## 1. Problem

Forward rendering evaluates every light against every shaded pixel regardless of whether the light reaches that pixel. At N lights and resolution W×H, the worst-case cost is N × W × H light evaluations per frame.

At 100 point lights and 1920×1080, that is approximately 207 million light evaluations per frame — before any other shading work. This does not scale. Even with trivial per-light cost, the redundant work becomes the dominant rendering bottleneck once a scene surpasses roughly 20 dynamic lights.

Tile-based light culling solves this by partitioning the screen into small tiles and determining, per tile, which lights could possibly contribute to pixels inside that tile. The fragment shader then iterates only the lights assigned to its tile instead of all lights in the scene. A tile touched by 8 of 100 lights runs 8 light evaluations instead of 100 — a 12x reduction in light work for that tile.

---

## 2. Tiled Forward Shading Overview

The screen is divided into a uniform grid of 16×16 pixel tiles. The number of tiles in each dimension:

```
TilesX = ceil(FramebufferWidth  / 16)
TilesY = ceil(FramebufferHeight / 16)
TotalTiles = TilesX * TilesY
```

At 1920×1080 this yields 120×68 = 8160 tiles.

For each tile, a compute shader reconstructs a view-space frustum from the four corner rays of the tile combined with a depth range read from the depth buffer or Hi-Z pyramid. Each light is then tested against each tile frustum. Lights that overlap a tile are appended to a flat `LightIndexList` buffer; a `LightGrid` buffer stores a (offset, count) pair per tile into that list.

The fragment shader computes its tile index from `gl_FragCoord`, reads the (offset, count) from `LightGrid`, and iterates only those entries in `LightIndexList`.

This technique is used in Battlefield 3 (Harada et al., 2012), DOOM (2016), and is the baseline implementation expected by most modern real-time rendering pipelines.

---

## 3. Light Buffer Layout

The existing light structs (`GpuDirectionLight`, `GpuPointLight`, `GpuSpotlight`) are kept unchanged. A header is prepended to the combined light UBO/SSBO to communicate counts to shaders without additional descriptor bindings:

```cpp
struct GpuLightHeader {
    uint32_t PointLightCount;
    uint32_t SpotLightCount;
    uint32_t _pad0;
    uint32_t _pad1;
};

// Combined buffer layout (std430):
// [GpuLightHeader]
// [GpuPointLight  × MAX_POINT_LIGHTS]
// [GpuSpotlight   × MAX_SPOT_LIGHTS]
```

`MAX_POINT_LIGHTS` = 1024. `MAX_SPOT_LIGHTS` = 256. These are compile-time constants in `LightCullDefs.h`. Debug asserts fire if scene light counts exceed these values.

Directional lights are not tile-culled. There are at most one or two directional lights per scene and they affect all pixels uniformly.

---

## 4. Tile Frustum Generation

A compute dispatch with one thread per tile generates the frustum planes used for culling.

Dispatch size: `(TilesX, TilesY, 1)`.

Each thread:
1. Computes the tile's four corner positions in NDC.
2. Unprojects the NDC corners through the inverse projection matrix to view space.
3. Constructs four side planes from adjacent corner ray pairs and the origin.
4. Reads the min/max depth for the tile from the Hi-Z pyramid (or a linearised depth min/max pass in v1 if Hi-Z is not yet available).
5. Constructs near and far planes from the depth range.
6. Writes the six planes to a `TileFrustum` entry in a storage buffer.

```cpp
struct TileFrustum {
    Vec4f Planes[6];  // normal.xyz + offset.w; all in view space
};
```

Storage buffer: `"tile_frustums"`, element count `TotalTiles`, written once per frame on camera or resolution change (cached otherwise).

The frustum generation pass runs before `LightCullPass`. It is skipped if the camera view/projection matrices and resolution are unchanged from the previous frame.

---

## 5. Light-Tile Assignment Pass

A second compute dispatch assigns lights to tiles and writes the `LightGrid` and `LightIndexList` buffers.

**Dispatch strategy:** One thread group per tile (`TilesX × TilesY` groups, 256 threads per group). Each group loads its tile frustum, then iterates all point lights and all spot lights, testing each against the frustum using sphere-vs-frustum and cone-vs-frustum tests. Passing lights are appended to a per-group intermediate list; after all lights are tested, the group does an atomic append into the global `LightIndexList` and writes its offset + count into `LightGrid`.

```glsl
// Pseudo-GLSL for the culling CS
shared uint s_light_indices[MAX_LIGHTS_PER_TILE];
shared uint s_light_count;

void main() {
    if (gl_LocalInvocationIndex == 0) s_light_count = 0;
    barrier();

    uint tile_idx = gl_WorkGroupID.y * TilesX + gl_WorkGroupID.x;
    TileFrustum frustum = tile_frustums[tile_idx];

    // Each thread tests a subset of lights
    for (uint i = gl_LocalInvocationIndex; i < PointLightCount; i += 256) {
        if (sphere_vs_frustum(point_lights[i].Position, point_lights[i].Radius, frustum)) {
            uint slot = atomicAdd(s_light_count, 1);
            if (slot < MAX_LIGHTS_PER_TILE) {
                s_light_indices[slot] = i;
            } else {
                // Overflow: more lights affect this tile than MAX_LIGHTS_PER_TILE.
                // This causes visual artifacts (missing lights). Report to CPU via debug buffer.
                atomicAdd(g_overflow_tile_count, 1u);
            }
        }
    }
    barrier();

    if (gl_LocalInvocationIndex == 0) {
        uint offset = atomicAdd(global_light_index_counter, s_light_count);
        light_grid[tile_idx].Offset = offset;
        light_grid[tile_idx].Count  = s_light_count;
        for (uint j = 0; j < s_light_count; ++j)
            light_index_list[offset + j] = s_light_indices[j];
    }
}
```

`MAX_LIGHTS_PER_TILE` = 256 (shared memory budget consideration; tune per GPU).

**CPU-side overflow check:** In `BeginFrame`, read `g_overflow_tile_count` back from the GPU debug buffer. If non-zero, log: `ZENGINE_CORE_WARN("LightCulling: %u tiles exceeded MAX_LIGHTS_PER_TILE this frame. Increase MAX_LIGHTS_PER_TILE or reduce light density.", count);` and reset the counter to zero for the next frame. Overflow is a content authoring error — it means too many lights overlap in a tile — and must be surfaced to the developer rather than silently dropped.

**`LightGrid` buffer layout:**

```cpp
struct LightGridEntry {
    uint32_t Offset;
    uint32_t Count;
};
// Size: TotalTiles × sizeof(LightGridEntry)
```

**`LightIndexList` buffer layout:** flat `uint32_t` array. Maximum size = `TotalTiles × MAX_LIGHTS_PER_TILE`. At 8160 tiles × 256 entries × 4 bytes = ~8.4 MB. Acceptable.

---

## 6. `LightCullPass : IRenderGraphCallbackPass`

```cpp
class LightCullPass final : public IRenderGraphCallbackPass {
public:
    void Setup(RenderGraphBuilder& builder) override;
    void Compile(RenderGraphInspector& inspector) override;
    void Execute(VkCommandBuffer cmd, RenderGraphInspector& inspector) override;

private:
    VkPipeline       m_frustum_pipeline = VK_NULL_HANDLE;
    VkPipeline       m_cull_pipeline    = VK_NULL_HANDLE;
    VkPipelineLayout m_layout           = VK_NULL_HANDLE;
    uint32_t         m_tiles_x          = 0;
    uint32_t         m_tiles_y          = 0;
};
```

**Setup:**

```cpp
void LightCullPass::Setup(RenderGraphBuilder& builder) {
    // Read inputs
    builder.ReadBuffer("hiz_depth",    BUFFER_USAGE_STORAGE_READ);
    builder.ReadBuffer("light_buffer", BUFFER_USAGE_UNIFORM_READ);

    // Write outputs
    builder.WriteBuffer("light_grid",
        RGBufferDesc{ .size  = TotalTiles * sizeof(LightGridEntry),
                      .usage = BUFFER_SET | BUFFER_STORAGE });

    builder.WriteBuffer("light_index_list",
        RGBufferDesc{ .size  = TotalTiles * MAX_LIGHTS_PER_TILE * sizeof(uint32_t),
                      .usage = BUFFER_SET | BUFFER_STORAGE });
}
```

**Compile:** creates compute pipelines from `tile_frustum.comp.glsl` and `light_cull.comp.glsl` if not already compiled; queries framebuffer dimensions to compute `m_tiles_x` and `m_tiles_y`.

**Execute:**

1. Zero `global_light_index_counter` via `vkCmdFillBuffer`.
2. Dispatch frustum generation: `(m_tiles_x, m_tiles_y, 1)`.
3. Pipeline barrier: compute writes → compute reads.
4. Dispatch light-tile assignment: `(m_tiles_x, m_tiles_y, 1)`.
5. Pipeline barrier: compute writes → fragment reads.

---

## 7. LightingPass Integration

`LightingPass` (the full-screen lighting evaluation in the forward path) binds `light_grid` and `light_index_list` as descriptor set resources. The fragment shader computes its tile index from `gl_FragCoord`:

```glsl
uint tile_x = uint(gl_FragCoord.x) / 16;
uint tile_y = uint(gl_FragCoord.y) / 16;
uint tile_idx = tile_y * TilesX + tile_x;

uint offset = light_grid[tile_idx].Offset;
uint count  = light_grid[tile_idx].Count;

vec3 total_radiance = vec3(0.0);
for (uint i = 0; i < count; ++i) {
    uint light_idx = light_index_list[offset + i];
    total_radiance += evaluate_point_light(point_lights[light_idx], surface);
}
```

The typical per-tile light count in a well-authored scene is 4–8 active lights even when the scene total is 100+. The worst case (many overlapping lights near the camera) degrades toward the unculled cost but remains bounded at `MAX_LIGHTS_PER_TILE`.

---

## 8. Cluster-Based Variant (v2)

Tiled culling is 2D: depth variation within a tile is captured only coarsely via the depth range. In scenes with extreme depth ranges (outdoor environments with foreground objects at 1m and background geometry at 5000m), many tiles will have a large depth range, causing lights at different depths to both pass the frustum test.

Clustered shading addresses this by subdividing each tile into depth slices (typically 24 slices, log-distributed along Z). This produces a 3D grid of clusters (16×16×24 is standard). Each cluster has a tighter frustum. Culling quality improves significantly in open-world and outdoor scenes.

Deferred to v2. The additional complexity (3D index buffer, log-depth slicing, exponential depth computation in the fragment shader) is not warranted until tiled forward is validated and profiled. The API surface of `LightCullPass` is designed so the output buffers (`light_grid`, `light_index_list`) are format-compatible: clustered shading simply changes the tile index computation in the fragment shader from 2D to 3D.

---

## 9. Light Count Limits and Debug Validation

Compile-time constants in `LightCullDefs.h`:

```cpp
constexpr uint32_t MAX_POINT_LIGHTS      = 1024;
constexpr uint32_t MAX_SPOT_LIGHTS       = 256;
constexpr uint32_t MAX_LIGHTS_PER_TILE   = 256;
constexpr uint32_t TILE_SIZE_PIXELS      = 16;
```

In `LightSystem::PrepareLightBuffer()`:

```cpp
ZENGINE_ASSERT(point_light_count <= MAX_POINT_LIGHTS,
    "Scene exceeds MAX_POINT_LIGHTS (%u). Increase or cull scene lights.", MAX_POINT_LIGHTS);
ZENGINE_ASSERT(spot_light_count <= MAX_SPOT_LIGHTS,
    "Scene exceeds MAX_SPOT_LIGHTS (%u).", MAX_SPOT_LIGHTS);
```

These asserts fire in debug builds. In release, excess lights are silently ignored (last lights in the array are dropped).

---

## 10. File Layout

```
ZEngine/Rendering/Lighting/
    LightCullDefs.h          -- compile-time constants, TileFrustum, LightGridEntry
    LightCullPass.h
    LightCullPass.cpp
    Shaders/
        tile_frustum.comp.glsl
        light_cull.comp.glsl
        light_cull_common.glsl   -- shared struct definitions and frustum test functions
```

`LightingPass` fragment shader changes are in `ZEngine/Rendering/Lighting/Shaders/lighting.frag.glsl` (existing file, additive change).

---

## 11. Deliverables Checklist

- [ ] `LightCullDefs.h` — constants, `TileFrustum`, `LightGridEntry`
- [ ] `tile_frustum.comp.glsl` — frustum generation compute shader
- [ ] `light_cull.comp.glsl` — light-tile assignment compute shader
- [ ] `LightCullPass.h` / `LightCullPass.cpp` — RenderGraph pass implementation
- [ ] `LightingPass` fragment shader updated to read `light_grid` and `light_index_list`
- [ ] `light_grid` and `light_index_list` registered as named RenderGraph resources
- [ ] Debug asserts for `MAX_POINT_LIGHTS` and `MAX_SPOT_LIGHTS` in `LightSystem`
- [ ] Integration test: 100 point lights scene, verify per-tile light count in RenderDoc
- [ ] Performance regression test: baseline forward vs tiled forward, 100 lights, 1080p
- [ ] GPU timestamp around `LightCullPass::Execute` plumbed to profiler overlay

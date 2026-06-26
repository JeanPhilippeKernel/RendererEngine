# ZEngine — Frustum and Occlusion Culling

**Priority:** Next-year plan — required for performance in scenes with 1000+ objects
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `render-graph-integration.md`, `lod-system.md`

---

## 1. Why Culling

Without culling, the renderer submits a draw call for every entity in the scene every frame regardless of whether that entity is visible. For a scene with 2000 entities, 60% of which are behind the camera or behind opaque geometry, this wastes both CPU time (recording draw commands) and GPU time (vertex processing for invisible triangles).

Two-stage culling eliminates this waste:

- **Frustum culling** runs on the CPU. It removes all objects outside the view frustum using a simple sphere-vs-plane test. No GPU involvement. Typical elimination rate: 50–70% of scene objects, achieved in under 0.5ms for 2000 entities with SIMD.
- **Occlusion culling** runs on the GPU. It removes objects that are within the frustum but hidden behind other geometry (terrain, buildings, walls). Elimination rate varies by scene; in a dense urban scene it can reach 60–80% of remaining frustum-visible objects.

Together they can reduce the submitted draw call count from 2000 to under 200 for a typical outdoor scene with buildings.

---

## 2. Bounding Volumes

Every entity that participates in culling must have a bounding volume. ZEngine uses bounding spheres for culling (cheaper than AABB for frustum tests; axis-aligned boxes are also supported for occlusion).

```cpp
// ZEngine/Rendering/Culling/BoundingVolumes.h

struct BoundingSphereComponent {
    Vec3f Center = {};    // object-space center (not world-space; transformed at cull time)
    float Radius = 0.f;
};

struct BoundingAABBComponent {
    Vec3f Min = {};       // object-space AABB minimum corner
    Vec3f Max = {};       // object-space AABB maximum corner
};
```

Both are plain-data ECS components. They are generated offline from the mesh data during the cook step and stored in the asset pack alongside the mesh. `MeshImporter` reads them and adds the components to the entity when the asset is instantiated.

The bounding sphere is a conservative enclosure of the AABB: center is the AABB midpoint, radius is half the diagonal length. This is intentionally conservative — it produces zero false culls (objects incorrectly removed) at the cost of occasional false positives (objects tested but not visible).

---

## 3. Frustum Culling

### 3.1 Plane Extraction

The view frustum is six half-spaces. Each half-space is defined by a plane equation `dot(n, p) + d >= 0` for visible points. The six planes (left, right, top, bottom, near, far) are extracted from the view-projection matrix each frame.

```cpp
// ZEngine/Rendering/Culling/FrustumCull.h

struct Frustum {
    Vec4f Planes[6];  // xyz = normal (unit length), w = distance from origin
};

Frustum ExtractFrustum(const Mat4f& view_proj);
```

Extraction uses the standard Gribb-Hartmann method (row-vector form of the VP matrix). The normals are normalized so the dot-product test gives true signed distance.

### 3.2 FrustumCullSystem

```cpp
// ZEngine/Rendering/Culling/FrustumCullSystem.h

class FrustumCullSystem {
public:
    void Initialize(Scene& scene);
    void Tick(Scene& scene, float dt);
    SystemDeps GetDeps() const;
};
```

Tick logic:

1. Read `CameraState` from scene singleton to get the current view-projection matrix.
2. Call `ExtractFrustum(view_proj)` to get the six planes.
3. For each entity with `(TransformComponent, BoundingSphereComponent)`:
   - Transform sphere center to world space using `TransformComponent.WorldTransform`.
   - Test the world-space sphere against all six planes.
   - If the sphere is entirely outside any single plane, the entity is culled.
   - Write result: add `CulledComponent` if culled, remove it if visible.

The sphere-plane test for one plane:

```cpp
float dist = dot(plane.xyz, sphere_center_ws) + plane.w;
if (dist < -sphere.Radius) { /* outside this plane */ }
```

A sphere is culled only if it fails the test for at least one plane. If it passes all six, it is considered visible (may still be occluded — that is handled by the GPU pass).

### 3.3 SOA Layout for SIMD

For scenes with thousands of entities, iterating sphere centers one-by-one is memory-inefficient. `FrustumCullSystem` operates on a SOA (Structure of Arrays) buffer maintained by the ECS for entities with `BoundingSphereComponent`:

```
float Centers_X[N];
float Centers_Y[N];
float Centers_Z[N];
float Radii[N];
```

The frustum plane test for all six planes against 8 spheres simultaneously fits in 256-bit AVX registers. The ECS SOA buffer is filled once per frame as a pre-pass before the main loop. The implementation uses intrinsics directly (no STL, no std::function).

The SOA buffer is allocated from the frame arena (`ArenaAllocator` with frame lifetime) — no heap allocation per frame.

SystemDeps mask:

```cpp
SystemDeps FrustumCullSystem::GetDeps() const {
    SystemDeps d;
    d.ReadComponents  = ComponentMask::TransformComponent
                      | ComponentMask::BoundingSphereComponent
                      | ComponentMask::CameraState;
    d.WriteComponents = ComponentMask::CulledComponent;
    return d;
}
```

---

## 4. CulledComponent Tag

`CulledComponent` is an empty tag component. Its presence on an entity means the entity is not visible this frame and should not be submitted as a draw call.

```cpp
// ZEngine/Rendering/Culling/CullingComponents.h

struct CulledComponent {};       // tag: frustum-culled this frame
struct OccludedComponent {};     // tag: GPU occlusion-culled this frame (1-frame lag)
```

The GeometryPass skips entities with either tag. The LOD system also reads `CulledComponent` to avoid computing screen-space size for invisible entities.

Both tags are removed at the start of each frame (before the cull systems run) so the slate is clean.

---

## 5. GPU Occlusion Culling — Hi-Z Approach

Frustum culling cannot remove objects that are within the frustum but behind a mountain or a wall. GPU occlusion culling catches these using the Hi-Z (hierarchical depth) technique.

Overview:

1. At the end of the previous frame, the depth buffer contains correct depth values for all visible geometry.
2. A compute pass downsamples this depth buffer into a mip chain (the Hi-Z pyramid). Each texel at each mip level stores the maximum depth of the corresponding region.
3. At the start of the current frame, before submitting opaque geometry, each entity's screen-projected AABB is tested against the appropriate mip level of the Hi-Z pyramid.
4. If the entity's minimum depth (nearest point to camera) is greater than the Hi-Z texel's maximum depth, the entity is entirely behind existing geometry and can be culled.
5. This introduces a one-frame lag: the pyramid is from the previous frame. Fast-moving cameras can produce false culls for one frame, which manifests as a one-frame pop. This is an accepted trade-off for the performance gain.

---

## 6. Hi-Z Pyramid Pass

The Hi-Z pyramid is built as a compute RenderGraph pass each frame, immediately after the depth pre-pass resolves.

```cpp
// ZEngine/Rendering/Culling/HiZBuildPass.h

class HiZBuildPass : public IRenderGraphCallbackPass {
public:
    void Setup(RenderGraphResourceBuilder& builder) override;
    void Execute(RenderGraphResourceInspector& inspector,
                 VkCommandBuffer cmd) override;

private:
    RenderGraphTextureHandle m_DepthInput;
    RenderGraphTextureHandle m_HiZOutput;  // named "hiz_depth"
};
```

The pass:

- Declares read on the previous frame's resolved depth buffer (named `"depth_prepass_resolved"`).
- Declares write on a new texture named `"hiz_depth"` (format: `VK_FORMAT_R32_SFLOAT`, mip count = `floor(log2(max(width, height))) + 1`).
- The compute shader is dispatched once per mip level. Each dispatch reads the previous mip level and writes the current mip, taking the maximum of each 2x2 block (conservative: assumes worst-case depth).

The compute shader (GLSL, reduced to essentials):

```glsl
// Reads prev_mip, writes curr_mip
// Each thread: sample 4 texels in prev_mip, output max(a,b,c,d) to curr_mip
layout(set=0, binding=0) uniform sampler2D prev_mip;
layout(set=0, binding=1, r32f) uniform writeonly image2D curr_mip;

void main() {
    ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
    float a = texelFetch(prev_mip, uv * 2 + ivec2(0,0), 0).r;
    float b = texelFetch(prev_mip, uv * 2 + ivec2(1,0), 0).r;
    float c = texelFetch(prev_mip, uv * 2 + ivec2(0,1), 0).r;
    float d = texelFetch(prev_mip, uv * 2 + ivec2(1,1), 0).r;
    imageStore(curr_mip, uv, vec4(max(max(a,b), max(c,d))));
}
```

The name `"hiz_depth"` is a stable RenderGraph resource name. Other passes that need the Hi-Z pyramid reference it by name via `RenderGraphResourceBuilder::DeclareRead("hiz_depth")`.

---

## 7. Occlusion Cull Pass

The occlusion cull pass dispatches one compute thread per entity. Each thread projects the entity's AABB into screen space, selects the appropriate Hi-Z mip level (based on projected area), and tests the entity's minimum Z against the Hi-Z sample.

```cpp
// ZEngine/Rendering/Culling/OcclusionCullPass.h

class OcclusionCullPass : public IRenderGraphCallbackPass {
public:
    void Setup(RenderGraphResourceBuilder& builder) override;
    void Execute(RenderGraphResourceInspector& inspector,
                 VkCommandBuffer cmd) override;

private:
    RenderGraphTextureHandle  m_HiZPyramid;
    RenderGraphBufferHandle   m_EntityAABBBuffer;    // read: per-entity world AABB
    RenderGraphBufferHandle   m_VisibilityBuffer;    // write: one uint32 per 32 entities (bitmask)
};
```

The GPU-side entity AABB buffer is uploaded each frame from the ECS `BoundingAABBComponent` + `TransformComponent` data. This upload happens in a CPU pre-pass before the `OcclusionCullPass` is executed.

The visibility bitmask buffer:

- One bit per entity index.
- Bit = 1 means visible (passed occlusion test).
- Bit = 0 means occluded.
- Size = `ceil(entity_count / 32) * sizeof(uint32_t)`.

The compute shader test (conceptual):

```glsl
// Project AABB corners to clip space
// Find screen-space AABB (min/max of projected corners)
// Compute mip level: level = ceil(log2(max(screen_w, screen_h)))
// Sample Hi-Z at that level
// If entity min-Z (nearest corner) > hiz_sample: entity is occluded
```

The mip level selection ensures the single Hi-Z texel sample conservatively covers the entire projected footprint of the entity.

---

## 8. Async Readback and 1-Frame Delay

The visibility bitmask lives on the GPU. To act on it on the CPU (to set `OccludedComponent` tags), the data must be read back. A synchronous readback would stall the CPU until the GPU finishes the dispatch — unacceptable.

Strategy: double-buffer the visibility bitmask. Frame N reads back frame N-1's result.

```
Frame N:   GPU writes visibility_buffer[N % 2]
           CPU reads  visibility_buffer[(N-1) % 2]  -> sets OccludedComponent
Frame N+1: GPU writes visibility_buffer[(N+1) % 2]
           CPU reads  visibility_buffer[N % 2]
```

The readback uses a `VkBuffer` with `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT` and a `vkMapMemory` persistent mapping. The CPU reads from this buffer at the start of the next frame before the ECS systems run. This adds exactly one frame of occlusion lag, which is imperceptible at 60 fps.

Before CPU reads from the mapped buffer, a pipeline barrier and cache invalidation are required:

```cpp
// BARRIER PLACEMENT: The pipeline barrier MUST be recorded in the command buffer
// BEFORE vkQueueSubmit, not after vkWaitForFences.
// After the fence signals, GPU execution is complete; a barrier at that point is a no-op.
//
// Correct sequence in OcclusionCullPass::Execute():
//
//   Step 1: Record compute dispatch (occlusion test)
//   Step 2: Record pipeline barrier (COMPUTE → HOST) in the SAME command buffer
//   Step 3: End command buffer recording
//   Step 4: vkQueueSubmit(cmd_buffer)
//   Step 5: vkWaitForFences(...)   ← GPU work complete; barrier has already taken effect
//   Step 6: vkInvalidateMappedMemoryRanges(...)  ← flush CPU cache
//   Step 7: Read visibility_mapped[(N-1) % 2]

// Step 2 — barrier in command buffer BEFORE submission:
VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
barrier.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.buffer              = m_visibility_buffer[(N-1) % 2];
barrier.offset              = 0;
barrier.size                = VK_WHOLE_SIZE;
vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_HOST_BIT,
    0, 0, nullptr, 1, &barrier, 0, nullptr);
// End command buffer, then submit.
// After fence signals:
VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
range.memory = m_visibility_memory[(N-1) % 2];
range.offset = 0;
range.size   = VK_WHOLE_SIZE;
vkInvalidateMappedMemoryRanges(device, 1, &range);
// NOW safe to read m_visibility_mapped[(N-1) % 2]
```

The `OcclusionCullReadbackSystem` runs at frame start, before `FrustumCullSystem`:

```cpp
class OcclusionCullReadbackSystem {
public:
    void Tick(Scene& scene, float dt);
    // Reads previous frame's visibility bitmask
    // Sets/clears OccludedComponent on each entity
    SystemDeps GetDeps() const;
};
```

---

## 9. Integration with Render Submission

The GeometryPass skips entities tagged with either `CulledComponent` or `OccludedComponent`. The check is a simple bitmask test on the entity's component presence flags — no virtual dispatch, no branching on entity type.

```cpp
// GeometryPass inner loop (simplified):
scene.ForEach<TransformComponent, RenderableComponent>([&](EntityID entity, ...) {
    if (scene.HasComponent<CulledComponent>(entity))   return;
    if (scene.HasComponent<OccludedComponent>(entity)) return;
    // submit draw
});
```

The order of system execution per frame:

1. `OcclusionCullReadbackSystem` — applies previous frame's GPU results to ECS tags.
2. `FrustumCullSystem` — tests frustum, sets/clears `CulledComponent`.
3. `LODSystem` — selects active LOD level for visible entities (skips culled ones).
4. `HiZBuildPass` (RenderGraph) — builds Hi-Z pyramid from previous frame's depth.
5. `OcclusionCullPass` (RenderGraph) — writes new visibility bitmask.
6. `GeometryPass` (RenderGraph) — submits draws, skipping culled and occluded entities.

Systems 1–3 are ECS systems scheduled by the `WorldTick`. Passes 4–6 are RenderGraph passes scheduled by the render graph compiler. The hand-off between ECS systems and RenderGraph passes is through the shared GPU AABB buffer (written by the ECS pre-pass before the RenderGraph executes).

### v2: Indirect Draw

In v2, the visibility bitmask stays entirely on the GPU. The OcclusionCullPass writes a compact `VkDrawIndexedIndirectCommand` buffer containing only visible entities. The GeometryPass uses `vkCmdDrawIndexedIndirect` to dispatch all draws in a single call without CPU readback. This eliminates the one-frame lag and the CPU iteration loop. Noted here; implementation deferred.

---

## 10. File Layout

```
ZEngine/Rendering/Culling/
    BoundingVolumes.h           -- BoundingSphereComponent, BoundingAABBComponent
    CullingComponents.h         -- CulledComponent, OccludedComponent tag structs
    FrustumCull.h               -- Frustum struct, ExtractFrustum()
    FrustumCull.cpp             -- plane extraction, SIMD sphere-plane test
    FrustumCullSystem.h         -- FrustumCullSystem class declaration
    FrustumCullSystem.cpp       -- SOA loop, SIMD implementation
    HiZBuildPass.h              -- HiZBuildPass : IRenderGraphCallbackPass
    HiZBuildPass.cpp            -- Setup/Execute, mip dispatch
    OcclusionCullPass.h         -- OcclusionCullPass : IRenderGraphCallbackPass
    OcclusionCullPass.cpp       -- AABB upload, compute dispatch, bitmask buffer
    OcclusionCullReadbackSystem.h
    OcclusionCullReadbackSystem.cpp  -- double-buffer readback, OccludedComponent writes

ZEngine/Rendering/Shaders/
    hiz_build.comp              -- Hi-Z pyramid mip generation compute shader
    occlusion_cull.comp         -- AABB occlusion test compute shader
```

---

## 11. Deliverables Checklist

- [ ] `BoundingSphereComponent` and `BoundingAABBComponent` structs defined and registered in ECS
- [ ] Cook pipeline generates bounding sphere and AABB from mesh data and stores in asset pack
- [ ] `MeshImporter` adds bounding volume components when instantiating mesh entities
- [ ] `ExtractFrustum` implemented and unit-tested against known frustum configurations
- [ ] `FrustumCullSystem::Tick` implemented with SOA SIMD sphere-plane test
- [ ] `CulledComponent` tag properly added/removed each frame
- [ ] `hiz_build.comp` shader implemented and reviewed for max-depth correctness
- [ ] `HiZBuildPass` integrated into RenderGraph after depth pre-pass
- [ ] `OcclusionCullPass` implemented with AABB upload, compute dispatch, bitmask output
- [ ] Double-buffered visibility bitmask with persistent host-visible mapping
- [ ] `OcclusionCullReadbackSystem` applies previous frame's bitmask to `OccludedComponent`
- [ ] GeometryPass skips entities with `CulledComponent` or `OccludedComponent`
- [ ] Integration test: 1000-entity scene with terrain occluder, assert GPU cull rate > 50%
- [ ] Performance test: frustum cull 2000 entities in under 0.5ms on CPU
- [ ] v2 indirect draw path documented and deferred

# ZEngine — Particle / VFX System

**Priority:** P3 — Required for visual polish (impacts, explosions, ambient effects)
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `render-resource-manager.md`
**Blocks:** Visual polish
**Approach:** CPU simulation with GPU instanced rendering; max 65536 particles per emitter

---

## 1. Design Philosophy

The particle system is CPU-driven with GPU instanced rendering. Each frame, the CPU
updates every live particle: position, velocity, color, size, lifetime. The GPU receives
a tightly packed array of `GPUParticleInstance` structs and renders all particles from
one emitter in a single instanced draw call.

This choice is deliberate for v1:

- **Debuggable**: particle state is ordinary CPU memory; print it, watch it, assert on it.
- **Simple**: no compute shader, no indirect dispatch, no GPU readback. Vulkan complexity
  is limited to uploading a dynamic storage buffer each frame.
- **Sufficient**: for typical game effects (impacts, fire, sparks, rain, dust) running
  at 60 fps on the target hardware, CPU simulation of up to 65536 particles per emitter
  is not a bottleneck. Profile before adding GPU compute.
- **Hot-path conformant**: no virtual dispatch in simulation loops, no `new`/`delete`,
  no exceptions. SOA layout (§4) keeps simulation loops cache-friendly.

A v2 GPU-compute path (instanced indirect, compute-shader integration/kill) is a possible
future upgrade. The `EmitterConfig` and `ParticlePool` interfaces are designed to
accommodate it without a breaking API change.

---

## 2. ECS Component — `ParticleEmitterComponent`

`ParticleEmitterComponent` is a thin Tier 2 ECS component. It holds only the data needed
to link an entity to a `ParticleManager` pool entry. All simulation parameters live in
`EmitterConfig` inside `ParticleManager` — not here. This separation keeps the ECS
component small and avoids duplicating config data across entities that share the same
emitter profile.

```cpp
// ZEngine/Particles/ParticleEmitterComponent.h
#pragma once
#include <cstdint>

namespace ZEngine::Particles
{
    struct ParticleEmitterComponent
    {
        uint32_t EmitterHandle; // index into ParticleManager's pool; kInvalidHandle if unset
        bool     IsActive;      // false = no spawn, simulation of existing particles continues
        bool     Loop;          // if true, restarts when ElapsedTime >= Duration
        float    Duration;      // seconds; 0.f = infinite (never expires based on time)
        float    ElapsedTime;   // seconds since emitter was activated or last looped
    };

    static constexpr uint32_t kInvalidEmitterHandle = 0xFFFFFFFFu;
}
```

`IsActive = false` suppresses new spawns but does not kill live particles — they age
out naturally. To kill all live particles immediately, call `ParticleManager::ClearPool`.

`Loop = true` with `Duration > 0.f` causes `ElapsedTime` to reset to `0.f` by
`ParticleSpawnSystem` when it reaches `Duration`. `Loop = false` with `Duration > 0.f`
causes `IsActive` to be set to `false` after one pass.

---

## 3. `EmitterConfig`

`EmitterConfig` describes every tunable property of an emitter. It is stored inside
`ParticleManager` (§5), not in the ECS component. Multiple entities can reference the
same `EmitterHandle` to share a config, though in practice each entity typically owns
its own emitter handle.

```cpp
// ZEngine/Particles/EmitterConfig.h
#pragma once
#include <Core/Maths/Vec3f.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::Particles
{
    enum class EmitterShape : uint8_t
    {
        Point  = 0,  // all particles spawn at emitter origin
        Sphere = 1,  // uniform random on sphere surface of radius ShapeRadius
        Cone   = 2,  // random direction within ConeAngle of emitter +Z, at ShapeRadius
        Box    = 3,  // uniform random inside BoxHalfExtents
    };

    struct EmitterConfig
    {
        // Spawn
        float    SpawnRate;     // particles per second (fractional accumulator, §6.1)
        uint32_t MaxParticles;  // hard upper bound on live particles; default 1024; max 65536

        // Shape
        EmitterShape       Shape;
        float              ShapeRadius;     // used by Sphere and Cone
        float              ConeAngleDeg;    // half-angle of cone in degrees (Cone shape)
        Core::Maths::Vec3f BoxHalfExtents;  // used by Box shape

        // Lifetime
        float LifetimeMin;  // seconds; must be > 0
        float LifetimeMax;  // seconds; must be >= LifetimeMin

        // Velocity
        Core::Maths::Vec3f InitialVelocityMin;  // component-wise min random initial velocity
        Core::Maths::Vec3f InitialVelocityMax;  // component-wise max random initial velocity
        Core::Maths::Vec3f Gravity;             // world-space acceleration (e.g. {0,-9.8f,0})
        float              Drag;                // linear drag coefficient: v *= (1 - Drag * dt)
                                                // clamp Drag to [0, 1] at config validation time

        // Size
        float SizeStart;  // world-space billboard radius at birth
        float SizeEnd;    // world-space billboard radius at death; lerped by normalized lifetime

        // Color
        Core::Maths::Vec4f ColorStart;  // RGBA at birth  (0–1 range)
        Core::Maths::Vec4f ColorEnd;    // RGBA at death; lerped by normalized lifetime

        // Texture
        uint32_t TextureHandle;  // RenderResourceManager handle to sprite atlas
        uint32_t AtlasCols;      // number of columns in the atlas grid (>= 1)
        uint32_t AtlasRows;      // number of rows    in the atlas grid (>= 1)
        // AtlasFrame selection is per-particle, set at spawn, advanced over lifetime (§6.1).

        // Rendering
        bool SortByDepth;  // if true, sort particles back-to-front each frame (§9)
    };
}
```

`EmitterConfig` is a plain data struct. Validation (e.g., `LifetimeMin > 0`,
`MaxParticles <= 65536`, `AtlasCols >= 1`) is performed by `ParticleManager::CreateEmitter`
via `ZENGINE_VALIDATE_ASSERT`. No runtime validation in the hot path.

---

## 4. `ParticlePool` — SOA Per-Particle State

Particle state uses a Structure of Arrays (SOA) layout. Each field is a separate array.
Simulation passes iterate over exactly the fields they need, keeping accessed memory
contiguous. A pass that only updates positions and velocities does not load color data
into cache.

```cpp
// ZEngine/Particles/ParticlePool.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Maths/Vec3f.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::Particles
{
    // All arrays have the same length: [0, Count). Elements at index i describe
    // the same particle across all arrays.
    struct ParticlePool
    {
        Core::Containers::Array<Core::Maths::Vec3f> Positions;    // world-space
        Core::Containers::Array<Core::Maths::Vec3f> Velocities;   // world-space, units/sec
        Core::Containers::Array<Core::Maths::Vec4f> Colors;       // RGBA interpolated
        Core::Containers::Array<float>              Sizes;         // billboard radius, interpolated
        Core::Containers::Array<float>              Lifetimes;     // remaining lifetime (seconds)
        Core::Containers::Array<float>              MaxLifetimes;  // initial lifetime at spawn
        Core::Containers::Array<uint32_t>           AtlasFrames;  // current atlas frame index

        uint32_t Count;     // number of currently alive particles
        uint32_t Capacity;  // EmitterConfig::MaxParticles (fixed at creation)

        // Spawn accumulator: fractional count of particles waiting to be born.
        float    SpawnAccumulator;
    };
}
```

All seven particle arrays are allocated together from the emitter's sub-arena at
`CreateEmitter` time. `Count` grows toward `Capacity` as particles are born and shrinks
as they die. Dead particles are removed with swap-and-pop (§6.2) to keep all live
particles in indices `[0, Count)`.

---

## 5. `ParticleManager`

`ParticleManager` owns the pool of all active emitters. It allocates `EmitterConfig` and
`ParticlePool` data from a sub-arena derived from the main arena passed at initialization.
It does not perform simulation — that is the job of the ECS systems (§6).

```cpp
// ZEngine/Particles/ParticleManager.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Particles/EmitterConfig.h>
#include <Particles/ParticlePool.h>
#include <cstdint>

namespace ZEngine::Particles
{
    class ParticleManager
    {
    public:
        // Must be called before any other method.
        // max_emitters is the hard cap on simultaneous active emitters.
        void Initialize(Core::Memory::ArenaAllocator* arena, uint32_t max_emitters);

        // Allocate an emitter and pre-allocate its ParticlePool from a sub-arena.
        // Validates config via ZENGINE_VALIDATE_ASSERT.
        // Returns kInvalidEmitterHandle if the emitter pool is full.
        [[nodiscard]] uint32_t CreateEmitter(const EmitterConfig& config);

        // Return an emitter slot to the free list and zero its pool Count.
        // Existing particle data is not zeroed — it will be overwritten on next spawn.
        void DestroyEmitter(uint32_t handle);

        // Kill all live particles in an emitter pool without destroying the emitter.
        void ClearPool(uint32_t handle);

        // Accessors used by the ECS systems. Return nullptr if handle is invalid.
        ParticlePool*        GetPool  (uint32_t handle);
        const EmitterConfig* GetConfig(uint32_t handle) const;

        // Total number of live particles across all emitters (debug/stats).
        uint32_t TotalLiveParticles() const;

        // Maximum emitters as passed to Initialize.
        uint32_t MaxEmitters() const;

    private:
        Core::Memory::ArenaAllocator*               m_Arena       = nullptr;
        uint32_t                                    m_MaxEmitters = 0;
        Core::Containers::Array<EmitterConfig>      m_Configs;
        Core::Containers::Array<ParticlePool>       m_Pools;
        Core::Containers::Array<bool>               m_Active;  // which slots are in use

        // Allocates the seven SOA arrays of pool from a sub-arena region.
        void AllocPool(ParticlePool& pool, uint32_t capacity,
                       Core::Memory::ArenaAllocator* sub_arena);

        bool IsValidHandle(uint32_t handle) const;
    };
}
```

`CreateEmitter` allocates a sub-arena region sized for `config.MaxParticles` particles
across all seven arrays, then calls `AllocPool` to set up the `ParticlePool` pointers.
`DestroyEmitter` marks the slot inactive; it does not release the sub-arena memory
(arenas do not support free). If slots need reuse, `ClearPool` resets `Count` to 0
so the next `CreateEmitter` into the same slot can reuse it. A slot-recycling free list
tracks which handles are available.

---

## 6. ECS Systems

Three ECS systems drive the particle lifecycle. They run in a fixed order enforced by
the system scheduler (§11): Spawn → Simulate → Render. They are registered with
`SystemDeps` masks declaring their component read/write intent.

### 6.1 `ParticleSpawnSystem`

Iterates all entities with `ParticleEmitterComponent`. For each active emitter, spawns
new particles and advances the elapsed time.

```cpp
// ZEngine/Particles/ParticleSpawnSystem.h
#pragma once
#include <ECS/Scene.h>
#include <Particles/ParticleManager.h>

namespace ZEngine::Particles
{
    // Registration:
    //   SystemDeps{ .WriteMask = MaskBit(ComponentTypeOf<ParticleEmitterComponent>()) }
    void ParticleSpawnSystem(ECS::Scene& scene, float dt, ParticleManager& mgr);
}
```

Per entity loop pseudocode:

```
for each entity with ParticleEmitterComponent comp:
    if !comp.IsActive: skip

    TransformComponent& xform = scene.GetComponent<TransformComponent>(entity)
    EmitterConfig* cfg = mgr.GetConfig(comp.EmitterHandle)
    ParticlePool*  pool = mgr.GetPool(comp.EmitterHandle)

    // Advance elapsed time.
    comp.ElapsedTime += dt
    if (!comp.Loop && cfg->Duration > 0.f && comp.ElapsedTime >= cfg->Duration):
        comp.IsActive = false
    elif (comp.Loop && cfg->Duration > 0.f && comp.ElapsedTime >= cfg->Duration):
        comp.ElapsedTime = fmod(comp.ElapsedTime, cfg->Duration)

    // Accumulate fractional spawn count.
    pool->SpawnAccumulator += cfg->SpawnRate * dt
    uint32_t to_spawn = (uint32_t)pool->SpawnAccumulator
    pool->SpawnAccumulator -= (float)to_spawn

    // Cap at available slots.
    to_spawn = min(to_spawn, pool->Capacity - pool->Count)

    for i in [0, to_spawn):
        uint32_t idx = pool->Count++
        pool->Positions[idx]    = SampleShape(cfg, xform.Position, xform.Forward)
        pool->Velocities[idx]   = RandomVec3(cfg->InitialVelocityMin, cfg->InitialVelocityMax)
        pool->MaxLifetimes[idx] = RandomFloat(cfg->LifetimeMin, cfg->LifetimeMax)
        pool->Lifetimes[idx]    = pool->MaxLifetimes[idx]
        pool->Colors[idx]       = cfg->ColorStart
        pool->Sizes[idx]        = cfg->SizeStart
        pool->AtlasFrames[idx]  = 0   // or random frame if desired
```

`SampleShape` (§10) returns a world-space spawn position based on `cfg->Shape` and the
emitter transform.

### 6.2 `ParticleSimulateSystem`

Integrates all live particles and removes dead ones.

```cpp
// ZEngine/Particles/ParticleSimulateSystem.h
#pragma once
#include <ECS/Scene.h>
#include <Particles/ParticleManager.h>

namespace ZEngine::Particles
{
    // Registration:
    //   SystemDeps{ .ReadMask  = MaskBit(ComponentTypeOf<ParticleEmitterComponent>()),
    //               .WriteMask = 0 }  // modifies ParticlePool, not ECS components
    void ParticleSimulateSystem(ECS::Scene& scene, float dt, ParticleManager& mgr);
}
```

Inner loop (per emitter, per live particle, iterated backward for swap-and-pop safety):

```
for each entity with ParticleEmitterComponent comp:
    ParticlePool* pool = mgr.GetPool(comp.EmitterHandle)
    EmitterConfig* cfg = mgr.GetConfig(comp.EmitterHandle)

    uint32_t i = 0
    while i < pool->Count:
        pool->Lifetimes[i] -= dt

        if pool->Lifetimes[i] <= 0.f:
            // Swap-and-pop: replace dead particle with last live particle.
            uint32_t last = pool->Count - 1
            if i != last:
                pool->Positions[i]    = pool->Positions[last]
                pool->Velocities[i]   = pool->Velocities[last]
                pool->Colors[i]       = pool->Colors[last]
                pool->Sizes[i]        = pool->Sizes[last]
                pool->Lifetimes[i]    = pool->Lifetimes[last]
                pool->MaxLifetimes[i] = pool->MaxLifetimes[last]
                pool->AtlasFrames[i]  = pool->AtlasFrames[last]
            pool->Count--
            continue  // do not increment i; recheck the swapped-in particle

        // Integrate velocity and gravity.
        pool->Velocities[i] += cfg->Gravity * dt
        pool->Velocities[i] *= (1.f - cfg->Drag * dt)   // linear drag
        pool->Positions[i]  += pool->Velocities[i] * dt

        // Normalized lifetime [0, 1]: 1 = just born, 0 = about to die.
        float t = pool->Lifetimes[i] / pool->MaxLifetimes[i]
        float t_lerp = 1.f - t   // 0 at birth, 1 at death

        // Interpolate color and size.
        pool->Colors[i] = Lerp(cfg->ColorStart, cfg->ColorEnd, t_lerp)
        pool->Sizes[i]  = cfg->SizeStart + (cfg->SizeEnd - cfg->SizeStart) * t_lerp

        // Advance atlas frame linearly over lifetime (optional; can be randomized at spawn).
        uint32_t total_frames = cfg->AtlasCols * cfg->AtlasRows
        pool->AtlasFrames[i] = (uint32_t)(t_lerp * (float)total_frames)
                              % total_frames

        ++i
```

Drag clamping: if `cfg->Drag * dt > 1.f`, clamp the result to 0 to avoid sign flip.
Validated at config creation, but also guarded in simulation.

### 6.3 `ParticleRenderSystem`

Uploads per-particle GPU data and submits instanced draw calls. Called after simulation
each frame.

```cpp
// ZEngine/Particles/ParticleRenderSystem.h
#pragma once
#include <ECS/Scene.h>
#include <Particles/ParticleManager.h>
#include <Rendering/RenderResourceManager.h>
#include <Rendering/RenderGraph.h>

namespace ZEngine::Particles
{
    // Registration:
    //   SystemDeps{ .ReadMask = MaskBit(ComponentTypeOf<ParticleEmitterComponent>()) }
    void ParticleRenderSystem(ECS::Scene&                     scene,
                              ParticleManager&                mgr,
                              Rendering::RenderResourceManager& rrm,
                              RenderGraph&                    rg);
}
```

Per emitter:

1. If `cfg->SortByDepth`: sort (§9).
2. Build `GPUParticleInstance` array (§7) from pool data.
3. Upload to a dynamic storage buffer via `rrm.UploadDynamicBuffer(...)`.
4. Emit an `IRenderGraphCallbackPass` that binds the atlas texture, the storage buffer,
   and the particle pipeline, then calls `vkCmdDrawInstanced(quad_vertex_count=4, pool->Count, ...)`.

The instanced draw uses a unit quad vertex buffer (4 vertices, positions `{-1,-1}, {1,-1},
{-1,1}, {1,1}`) bound once at startup. The vertex shader reads instance data from the
storage buffer by `gl_InstanceIndex`.

---

## 7. GPU Data Layout

The storage buffer that the vertex shader reads is an array of `GPUParticleInstance`.
Padding ensures 16-byte alignment throughout; no `std430` packing surprises.

```cpp
// ZEngine/Particles/GPUParticleInstance.h
#pragma once
#include <Core/Maths/Vec3f.h>
#include <Core/Maths/Vec4f.h>

namespace ZEngine::Particles
{
    // std430 layout — no implicit padding between members.
    // Total size: 3*4 + 4 + 4*4 + 2*4 + 2*4 = 12 + 4 + 16 + 8 + 8 = 48 bytes.
    struct GPUParticleInstance
    {
        Core::Maths::Vec3f Position;   // world-space center of billboard
        float              Size;       // billboard half-extent in world units
        Core::Maths::Vec4f Color;      // RGBA (0–1); pre-multiplied alpha not required in v1
        float              AtlasU;     // horizontal UV offset for atlas frame (0–1)
        float              AtlasV;     // vertical   UV offset for atlas frame (0–1)
        float              Padding[2]; // align to 16 bytes
    };
    static_assert(sizeof(GPUParticleInstance) == 48,
        "GPUParticleInstance must be 48 bytes for correct std430 storage buffer layout");
    static_assert(alignof(GPUParticleInstance) == 4,
        "GPUParticleInstance must be 4-byte aligned");
}
```

`AtlasU` and `AtlasV` are the bottom-left UV corner of the particle's atlas frame:

```cpp
uint32_t frame = pool->AtlasFrames[i];
instance.AtlasU = (float)(frame % cfg->AtlasCols) / (float)cfg->AtlasCols;
instance.AtlasV = (float)(frame / cfg->AtlasCols) / (float)cfg->AtlasRows;
```

The vertex shader adds `(uv.x / AtlasCols, uv.y / AtlasRows)` to `(AtlasU, AtlasV)` to
compute the final sample coordinate for each corner of the quad.

The storage buffer is re-uploaded every frame from the CPU-side pool data. No
double-buffering in v1 — the frame graph handles synchronization via its existing
buffer management. A v2 optimization would use a persistent mapped buffer with a
per-frame ring offset.

---

## 8. Billboard Shader

### Vertex Shader (pseudocode — GLSL/HLSL equivalent)

```glsl
// Inputs
layout(location = 0) in vec2 QuadUV;  // per-vertex: {0,0} {1,0} {0,1} {1,1}

// Instance data from storage buffer
layout(std430, binding = 0) readonly buffer ParticleBuffer {
    GPUParticleInstance Particles[];
};

// Per-frame uniforms
layout(binding = 1) uniform FrameUniforms {
    mat4 ViewProj;
    vec3 CameraRight;  // world-space right vector of camera
    vec3 CameraUp;     // world-space up vector of camera
    uint AtlasCols;
    uint AtlasRows;
};

layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_TexCoord;

void main()
{
    GPUParticleInstance p = Particles[gl_InstanceIndex];

    // Build camera-facing billboard corner in world space.
    vec3 corner = p.Position
                + CameraRight * (QuadUV.x * 2.0 - 1.0) * p.Size
                + CameraUp    * (QuadUV.y * 2.0 - 1.0) * p.Size;

    gl_Position = ViewProj * vec4(corner, 1.0);

    // Compute atlas UV for this corner.
    float inv_cols = 1.0 / float(AtlasCols);
    float inv_rows = 1.0 / float(AtlasRows);
    v_TexCoord = vec2(p.AtlasU + QuadUV.x * inv_cols,
                      p.AtlasV + QuadUV.y * inv_rows);

    v_Color = p.Color;
}
```

`CameraRight` and `CameraUp` are extracted from the view matrix inverse and uploaded in
per-frame uniforms. The billboard is always camera-facing (spherical billboard). A
`CylindricalBillboard` mode (lock Y axis) can be added in v2 by zeroing the Y component
of `CameraRight`.

### Fragment Shader

```glsl
layout(binding = 2) uniform sampler2D AtlasTexture;

layout(location = 0) in vec4 v_Color;
layout(location = 1) in vec2 v_TexCoord;

layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 texSample = texture(AtlasTexture, v_TexCoord);
    FragColor = texSample * v_Color;
    // Alpha-blend: handled by Vulkan pipeline blend state (SRC_ALPHA, ONE_MINUS_SRC_ALPHA).
    // Discard transparent pixels to avoid overdraw cost in dense emitters:
    if (FragColor.a < 0.004) discard;
}
```

The Vulkan pipeline for particle rendering has:

- `VkPipelineColorBlendAttachmentState`: `srcColorBlendFactor = SRC_ALPHA`,
  `dstColorBlendFactor = ONE_MINUS_SRC_ALPHA`, `colorBlendOp = ADD`.
- Depth write disabled (`depthWriteEnable = VK_FALSE`).
- Depth test enabled (`depthTestEnable = VK_TRUE`, `compareOp = LESS_OR_EQUAL`).
- Back-face culling disabled (particles are single-sided quads from any viewing angle).

---

## 9. Depth Sorting

When `EmitterConfig::SortByDepth` is `true`, particles must be rendered back-to-front
relative to the camera to produce correct alpha-blending. Sorting the SOA arrays
directly would require swapping all seven arrays simultaneously, which is costly and
error-prone. Instead, sort an auxiliary index array.

```cpp
// In ParticleRenderSystem, when SortByDepth is true:

// Scratch arena allocation — freed implicitly at end of frame.
Core::Containers::Array<uint32_t> sorted_indices;
sorted_indices.Resize(pool->Count);
for (uint32_t i = 0; i < pool->Count; ++i) sorted_indices[i] = i;

// Sort indices by decreasing distance from camera.
Vec3f cam_pos = GetCameraPosition(scene);
std::sort(sorted_indices.Data(), sorted_indices.Data() + pool->Count,
    [&](uint32_t a, uint32_t b) {
        float da = LengthSq(pool->Positions[a] - cam_pos);
        float db = LengthSq(pool->Positions[b] - cam_pos);
        return da > db;  // descending: furthest first
    });

// Build GPUParticleInstance array in sorted order.
for (uint32_t j = 0; j < pool->Count; ++j)
{
    uint32_t i = sorted_indices[j];
    // ... fill gpu_instances[j] from pool index i
}
```

`LengthSq` (squared distance) avoids a `sqrt` — sort only requires relative ordering.

Sorting cost is O(N log N) per emitter per frame. For `SortByDepth = false` (most
emitters: fire, sparks, smoke that uses additive blending), skip sorting entirely.
Additive blending is order-independent and does not need a sort.

---

## 10. Emitter Spawn Shapes

`SampleShape` returns the world-space spawn position for a new particle, given the
emitter's `EmitterConfig` and the emitter entity's `TransformComponent`.

```cpp
// ZEngine/Particles/SpawnShapes.h
#pragma once
#include <Core/Maths/Vec3f.h>
#include <Particles/EmitterConfig.h>
#include <ECS/Components/TransformComponent.h>

namespace ZEngine::Particles
{
    Core::Maths::Vec3f SampleShape(
        const EmitterConfig&                     cfg,
        const ECS::Components::TransformComponent& xform,
        uint32_t& inout_rng_state);  // simple xorshift32 state
}
```

Implementation by shape:

**Point**
```
return xform.Position;
```

**Sphere** (uniform on sphere surface via rejection-free method):

```
// Marsaglia method: pick (u, v) in [-1,1]^2 with u^2+v^2 < 1.
do { u = RandomFloat(-1,1); v = RandomFloat(-1,1); } while (u*u + v*v >= 1.f);
float s = sqrt(1.f - u*u - v*v);
Vec3f dir = { 2*u*s, 2*v*s, 1 - 2*(u*u+v*v) };   // unit sphere point
return xform.Position + dir * cfg.ShapeRadius;
```

**Cone**:

```
float angle = RandomFloat(0, cfg.ConeAngleDeg * DEG_TO_RAD);
float phi   = RandomFloat(0, 2.f * PI);
// Direction within cone around emitter forward (+Z in local space, rotated by xform).
Vec3f local_dir = {
    sin(angle) * cos(phi),
    sin(angle) * sin(phi),
    cos(angle)
};
Vec3f world_dir = xform.Rotation * local_dir;  // apply emitter orientation
return xform.Position + world_dir * cfg.ShapeRadius;
```

**Box**:

```
return xform.Position + Vec3f {
    RandomFloat(-cfg.BoxHalfExtents.x, cfg.BoxHalfExtents.x),
    RandomFloat(-cfg.BoxHalfExtents.y, cfg.BoxHalfExtents.y),
    RandomFloat(-cfg.BoxHalfExtents.z, cfg.BoxHalfExtents.z)
};
```

The RNG is a per-emitter `xorshift32` state stored alongside the `ParticlePool` as
`pool->RNGState`. This keeps particle randomness deterministic per emitter and independent
of simulation order. No global RNG state is shared.

---

## 11. Scheduler Registration

All three systems are registered with the system scheduler from `actor-ecs-architecture.md`
and `system-scheduler.md`. Ordering is enforced via `OrderBefore` calls:

```cpp
// At engine initialization (game or editor world setup):
SystemID spawn_id = world.RegisterSystem(ParticleSpawnSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<ECS::Components::TransformComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<Particles::ParticleEmitterComponent>()),
});

SystemID simulate_id = world.RegisterSystem(ParticleSimulateSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<Particles::ParticleEmitterComponent>()),
    .WriteMask = 0,  // modifies ParticlePool via ParticleManager, not ECS components
});

SystemID render_id = world.RegisterSystem(ParticleRenderSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<Particles::ParticleEmitterComponent>()),
    .WriteMask = 0,
});

// Enforce spawn → simulate → render order.
world.OrderBefore(spawn_id,    simulate_id);
world.OrderBefore(simulate_id, render_id);
```

`ParticleSimulateSystem` and `ParticleRenderSystem` both declare `WriteMask = 0` for ECS
components — they write to `ParticlePool` data through `ParticleManager`, which is
external to the ECS component storage and therefore not tracked by the scheduler's
conflict rules. The `OrderBefore` calls enforce correct sequencing explicitly.

`ParticleSpawnSystem` and any physics or transform system that writes
`TransformComponent` must be ordered so transforms are stable before spawn reads them.
Add `OrderBefore(physics_id, spawn_id)` at registration time.

---

## 12. File Layout

```
ZEngine/
  Particles/
    EmitterConfig.h               — EmitterConfig struct, EmitterShape enum
    ParticleEmitterComponent.h    — ParticleEmitterComponent struct, kInvalidEmitterHandle
    ParticlePool.h                — ParticlePool struct (SOA layout)
    GPUParticleInstance.h         — GPUParticleInstance struct (GPU upload layout)
    ParticleManager.h             — ParticleManager class declaration
    ParticleManager.cpp           — CreateEmitter, DestroyEmitter, ClearPool, accessors
    ParticleSpawnSystem.h         — ParticleSpawnSystem declaration
    ParticleSpawnSystem.cpp       — spawn loop, SampleShape call, accumulator logic
    ParticleSimulateSystem.h      — ParticleSimulateSystem declaration
    ParticleSimulateSystem.cpp    — integrate, drag, lifetime, swap-and-pop, lerp
    ParticleRenderSystem.h        — ParticleRenderSystem declaration
    ParticleRenderSystem.cpp      — sort, GPU instance build, storage buffer upload, draw
    SpawnShapes.h                 — SampleShape declaration
    SpawnShapes.cpp               — Point / Sphere / Cone / Box implementations
  Shaders/
    Particles/
      particle.vert               — billboard vertex shader
      particle.frag               — atlas-sampling fragment shader
```

All files are in namespace `ZEngine::Particles`. No file in this module uses `new`,
`delete`, `std::unique_ptr`, or exceptions. All particle memory is allocated from the
`ArenaAllocator` passed to `ParticleManager::Initialize`. SOA arrays use
`Core::Containers::Array<T>` backed by that arena.

---

## 13. Deliverables Checklist

- [ ] `EmitterConfig.h` — full struct with all spawn/shape/lifetime/velocity/size/color/texture fields
- [ ] `ParticleEmitterComponent.h` — ECS component struct, `kInvalidEmitterHandle` constant
- [ ] `ParticlePool.h` — SOA struct with all seven particle arrays plus `Count`, `Capacity`, `SpawnAccumulator`
- [ ] `GPUParticleInstance.h` — GPU struct with `static_assert` on size (48 bytes)
- [ ] `ParticleManager.h` / `ParticleManager.cpp` — `Initialize`, `CreateEmitter` (with config
  validation), `DestroyEmitter`, `ClearPool`, `GetPool`, `GetConfig`, `TotalLiveParticles`
- [ ] `SpawnShapes.h` / `SpawnShapes.cpp` — all four shape implementations with xorshift32 RNG
- [ ] `ParticleSpawnSystem.h` / `ParticleSpawnSystem.cpp` — fractional accumulator spawn, shape
  dispatch, loop/duration logic
- [ ] `ParticleSimulateSystem.h` / `ParticleSimulateSystem.cpp` — gravity, drag, position
  integration, lifetime decrement, swap-and-pop, color/size lerp, atlas frame advance
- [ ] `ParticleRenderSystem.h` / `ParticleRenderSystem.cpp` — optional depth sort via index array,
  `GPUParticleInstance` build loop, dynamic storage buffer upload, instanced draw submission
- [ ] `particle.vert` — camera-facing billboard vertex shader with atlas UV computation
- [ ] `particle.frag` — atlas-sampled fragment shader with alpha discard
- [ ] Vulkan pipeline descriptor for particles: blend state, depth test on / depth write off
- [ ] Scheduler wiring: `spawn_id`, `simulate_id`, `render_id` registration with `OrderBefore` calls
- [ ] Unit tests: spawn accumulator produces correct particle count over N frames; swap-and-pop
  does not corrupt SOA arrays; `SortByDepth` index array matches manual sort
- [ ] Integration test: create emitter, tick 60 frames, verify `Count` stabilizes at
  `SpawnRate / (1 / LifetimeAvg)` (steady-state equation)
- [ ] Performance test: 4 emitters × 1024 particles each, 60 fps budget, verify CPU simulate
  time is under 0.5 ms on a mid-range CPU
- [ ] Validation test: `EmitterConfig` with `MaxParticles = 0` or `LifetimeMin <= 0` triggers
  `ZENGINE_VALIDATE_ASSERT` in debug build
  - [ ] Edge case: `LifetimeMin == 0` and `LifetimeMax == 0` — triggers ZENGINE_VALIDATE_ASSERT
  - [ ] Edge case: `SpawnRate == 0` — no particles spawned, emitter stays alive
  - [ ] Edge case: `MaxParticles == 1` — exactly one particle at a time
  - [ ] Edge case: `Gravity == Vec3f(0,0,0)` — particles move in straight lines only
  - [ ] Edge case: emitter transform changes mid-frame — spawn positions are consistent
  - [ ] Regression: SOA swap-and-pop during simulate does not corrupt other arrays
  - [ ] Stress: 65536 active particles across 64 emitters — no frame budget exceeded

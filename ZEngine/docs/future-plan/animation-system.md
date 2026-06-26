# ZEngine — Animation System

**Priority:** P2 — Implement after ECS core and scheduler are live (Phase 4 of migration-plan.md)  
**Status:** Design  
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `render-resource-manager.md` (`UploadBuffer` for bone matrix GPU upload)  
**Blocked by:** Phase 0 math prerequisites (Vec3 lerp, TRS), VFS Ticket 1

---

## 1. Overview

The animation system drives skeletal animation through three ECS components and two ECS
systems. It is entirely data-driven — no behavior lives on components. The GPU receives
a per-frame bone matrix buffer that the skinning shader reads.

```
AssimpImporter
  extracts skeleton + clips → AssetSkeleton, AssetAnimationClip

Runtime load
  AssetSkeleton      → SkeletonComponent    (bone hierarchy, bind pose)
  AssetAnimationClip → AnimationClipAsset   (clip pool, handle-based)

Per-frame pipeline (ECS systems):
  AnimationSampleSystem    reads AnimatorComponent, writes pose buffer
  SkinningUploadSystem     reads pose buffer + SkinningComponent, writes GPU bone buffer

Render system reads GPU bone buffer via RenderResourceManager.
```

---

## 2. ECS Components

All components are plain data structs. No virtual methods, no behavior.
Lives in `ZEngine::ECS::Components`.

### 2.1 `SkeletonComponent`

Holds the bone hierarchy and bind-pose inverse matrices for one skeleton.
Shared across all entities that use the same skeleton — holds a handle into
a skeleton pool, not inline data.

```cpp
// ZEngine/ECS/Components/SkeletonComponent.h
#pragma once
#include <Animation/AnimationHandles.h>

namespace ZEngine::ECS::Components {

    struct SkeletonComponent {
        Animation::SkeletonHandle Handle = Animation::INVALID_SKELETON;
    };

}  // namespace ZEngine::ECS::Components
```

The actual skeleton data (bone count, parent indices, inverse bind matrices) lives in
`Animation::AnimationManager` — see Section 3.4.

### 2.2 `AnimatorComponent`

Per-entity animation state: which clip is playing, current playback time, rate, loop flag,
and the output pose buffer (one local-space transform per bone for this entity this frame).

```cpp
// ZEngine/ECS/Components/AnimatorComponent.h
#pragma once
#include <Animation/AnimationHandles.h>

namespace ZEngine::ECS::Components {

    struct AnimatorComponent {
        Animation::AnimationClipHandle  ClipHandle      = Animation::INVALID_CLIP;
        Animation::PoseHandle           PoseHandle      = Animation::INVALID_POSE;
        float                           PlaybackTime    = 0.f;   // seconds
        float                           PlaybackRate    = 1.f;   // 1.0 = normal speed
        bool                            Loop            = true;
        bool                            Playing         = false;
        bool                            AllocateFailed  = false; // set if pose pool is exhausted; prevents infinite retry
    };

    static_assert(sizeof(AnimatorComponent) <= 32, "AnimatorComponent must fit in half a cache line");

}  // namespace ZEngine::ECS::Components
```

### 2.3 `SkinningComponent`

Links the entity to its GPU-side skinning resources: the vertex buffer handle (containing
bone indices and weights) and the GPU bone matrix buffer that the skinning shader reads.

```cpp
// ZEngine/ECS/Components/SkinningComponent.h
#pragma once
#include <cstdint>

namespace ZEngine::ECS::Components {

    using GpuBufferHandle = uint32_t;
    constexpr GpuBufferHandle INVALID_GPU_BUFFER = UINT32_MAX;

    struct SkinningComponent {
        GpuBufferHandle SkinDataBufferHandle    = INVALID_GPU_BUFFER;  // vertex bone indices + weights
        GpuBufferHandle BoneMatrixBufferHandle  = INVALID_GPU_BUFFER;  // written each frame by SkinningUploadSystem
        uint32_t        BoneCount               = 0;
    };

}  // namespace ZEngine::ECS::Components
```

---

## 3. Runtime Asset Types

These live in `ZEngine::Animation` alongside the pools. They are not ECS components —
they are the resource data that components hold handles into.

### 3.1 `SkeletonData`

```cpp
// ZEngine/Animation/SkeletonData.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Maths/Matrix.h>

namespace ZEngine::Animation {

    struct SkeletonData {
        uint32_t                                   BoneCount           = 0;
        Core::Containers::Array<int32_t>           ParentIndices;       // -1 = root bone
        Core::Containers::Array<Core::Maths::Mat4f> InverseBindMatrices;
        // BoneNames removed from runtime data — not needed by simulation systems.
        // Names are debug/editor-only; query via import metadata or AssetRegistry.
        // Storing Array<String> here violated DOD (non-POD in pool, unused in hot path).
    };

}  // namespace ZEngine::Animation
```

`ParentIndices[i]` is the index of bone i's parent, or -1 if it is the root.
`InverseBindMatrices[i]` transforms a vertex from model space into bone i's local space
(the standard skinning formula: `FinalMatrix[i] = GlobalPose[i] * InverseBindMatrix[i]`).

### 3.2 `AnimationClip`

Uniform-sample-rate clip. All channels sampled at the same rate — no per-key timestamps.
Sampling is a single array index + lerp, no binary search.

```cpp
// ZEngine/Animation/AnimationClip.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Maths/Matrix.h>
#include <Core/Maths/Quaternion.h>

namespace ZEngine::Animation {

    struct BoneChannel {
        Core::Containers::Array<Core::Maths::Vec3f>             PositionKeys;
        Core::Containers::Array<Core::Maths::Quaternion<float>>  RotationKeys;
        Core::Containers::Array<Core::Maths::Vec3f>             ScaleKeys;
    };

    struct AnimationClip {
        float                              DurationSeconds = 0.f;
        float                              SampleRate      = 30.f;  // keys per second
        uint32_t                           BoneCount       = 0;
        Core::Containers::Array<BoneChannel> Channels;              // one per bone, indexed by bone index
    };

}  // namespace ZEngine::Animation
```

**Sampling a channel at time `t`:**

```
frame_f    = t * SampleRate
frame_i    = (uint32_t)frame_f
alpha      = frame_f - frame_i
next_i     = min(frame_i + 1, key_count - 1)

position   = lerp(PositionKeys[frame_i], PositionKeys[next_i], alpha)
rotation   = slerp(RotationKeys[frame_i], RotationKeys[next_i], alpha)   // uses existing ZEngine slerp
scale      = lerp(ScaleKeys[frame_i], ScaleKeys[next_i], alpha)
```

Using the existing `ZEngine::Core::Maths::slerp` and `lerp` from `Quaternion.h`.

### 3.3 `AnimationHandles.h` — shared handle types

Handle types are defined in a standalone header with no other includes. Both
`AnimationManager.h` and the component headers include only this file, breaking any
circular include chain.

```cpp
// ZEngine/Animation/AnimationHandles.h
#pragma once
#include <cstdint>

namespace ZEngine::Animation {
    using SkeletonHandle       = uint32_t;
    using AnimationClipHandle  = uint32_t;
    using PoseHandle           = uint32_t;

    constexpr SkeletonHandle      INVALID_SKELETON = UINT32_MAX;
    constexpr AnimationClipHandle INVALID_CLIP      = UINT32_MAX;
    constexpr PoseHandle          INVALID_POSE      = UINT32_MAX;
}
```

Component headers (`SkeletonComponent.h`, `AnimatorComponent.h`) include only
`Animation/AnimationHandles.h`. `AnimationManager.h` also includes only
`AnimationHandles.h` plus the data types — never the component headers.

### 3.4 `AnimationManager`

Owns all runtime animation data and the per-entity pose buffers. One instance per engine.

```cpp
// ZEngine/Animation/AnimationManager.h
#pragma once
#include <Animation/AnimationHandles.h>
#include <Animation/SkeletonData.h>
#include <Animation/AnimationClip.h>
#include <Core/Containers/Array.h>
#include <Core/Maths/Quaternion.h>

namespace ZEngine::Animation {

    // Maximum bones per skeleton. Increase if a skeleton requires more.
    static constexpr uint32_t MAX_BONES_PER_SKELETON = 256;
    static constexpr uint32_t MAX_POSES              = 65536; // max simultaneously animated entities

    struct BoneTransform {
        Core::Maths::Vec3f             Position = {};   // 12 bytes
        Core::Maths::Quaternion<float> Rotation = {};   // 16 bytes
        Core::Maths::Vec3f             Scale    = {1.f, 1.f, 1.f}; // 12 bytes
        float                          _pad     = 0.f; // 4 bytes — align to 16
    };
    static_assert(sizeof(BoneTransform) == 48, "BoneTransform size mismatch");
    // Quaternion<float> is 16 bytes and aligns to 16 on most platforms (SIMD-friendly).
// The struct is 48 bytes total; natural alignment is 16, not 4.
static_assert(alignof(BoneTransform) == 16 || alignof(BoneTransform) == 4,
    "BoneTransform alignment unexpected — verify Quaternion<float> alignment on this platform");
// NOTE: If Quaternion<float> uses __m128 internally, alignof == 16 and the struct
// is SIMD-safe. If it is plain floats, alignof == 4. Assert the safer expectation:
static_assert(sizeof(BoneTransform) % 16 == 0,
    "BoneTransform size must be a multiple of 16 for array-of-structs SIMD access");

    class AnimationManager {
    public:
        // AnimationManager::Initialize must reserve m_poses to MAX_POSES capacity
        // so that AllocatePose never causes a reallocation. GetPose returns a raw
        // pointer that is valid for the lifetime of the manager ONLY if m_poses
        // never reallocates. Pre-reservation guarantees this.
        void Initialize(Core::Memory::ArenaAllocator* arena) {
            // ... existing init ...
            m_poses.Reserve(MAX_POSES);  // NO reallocation after this point
        }

        // Skeleton pool
        [[nodiscard]] SkeletonHandle       AddSkeleton(SkeletonData skeleton);
        [[nodiscard]] const SkeletonData*  GetSkeleton(SkeletonHandle handle) const;

        // Clip pool
        [[nodiscard]] AnimationClipHandle  AddClip(AnimationClip clip);
        [[nodiscard]] const AnimationClip* GetClip(AnimationClipHandle handle) const;

        // Pose buffer pool — one buffer per animated entity, pre-allocated at entity creation time.
        // Returns INVALID_POSE if pool is exhausted (MAX_POSES limit reached).
        [[nodiscard]] PoseHandle                        AllocatePose(uint32_t bone_count);

        // GetPose contract: valid as long as AnimationManager is alive and
        // m_poses has not been cleared. Callers must not cache this pointer
        // across AnimationManager::Clear() calls.
        [[nodiscard]] Core::Containers::Array<BoneTransform>* GetPose(PoseHandle handle);

    private:
        Core::Containers::Array<SkeletonData>                           m_skeletons;
        Core::Containers::Array<AnimationClip>                          m_clips;
        Core::Containers::Array<Core::Containers::Array<BoneTransform>> m_poses;
    };

}  // namespace ZEngine::Animation
```

---

## 4. ECS Systems

### 4.1 `AnimationSampleSystem`

Advances playback time and writes the local-space pose for each animated entity.

**Dependencies (for `system-scheduler.md`):**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<SkeletonComponent>())
               | MaskBit(ComponentTypeOf<AnimatorComponent>()),  // reads before writing
    .WriteMask = MaskBit(ComponentTypeOf<AnimatorComponent>()),
}
```

**Algorithm:**

```
ForEach<SkeletonComponent, AnimatorComponent>(scene, [dt, &anim_mgr](EntityID, SkeletonComponent& sk, AnimatorComponent& anim) {
    if (!anim.Playing || anim.ClipHandle == Animation::INVALID_CLIP) return;

    const AnimationClip* clip     = anim_mgr.GetClip(anim.ClipHandle);
    const SkeletonData*  skeleton = anim_mgr.GetSkeleton(sk.Handle);
    if (!clip || !skeleton) return;

    // Pose buffer is allocated at entity creation (Actor::OnCreate or component-add path),
    // NOT lazily here. If PoseHandle is still INVALID_POSE at sample time, it means
    // AllocatePose was never called or the pool was exhausted.
    if (anim.PoseHandle == Animation::INVALID_POSE) {
        if (anim.AllocateFailed) return; // pool was exhausted; already logged
        anim.PoseHandle = anim_mgr.AllocatePose(skeleton->BoneCount);
        if (anim.PoseHandle == Animation::INVALID_POSE) {
            anim.AllocateFailed = true;
            ZENGINE_CORE_WARN("AnimationSampleSystem: pose pool exhausted (MAX_POSES=%u). Entity skipped.", Animation::AnimationManager::MAX_POSES);
            return;
        }
    }
    auto* pose = anim_mgr.GetPose(anim.PoseHandle);
    if (!pose) return; // defensive: handle was valid but pool realloc invalidated pointer

    // Advance time
    anim.PlaybackTime += dt * anim.PlaybackRate;
    if (anim.Loop) {
        anim.PlaybackTime = fmod(anim.PlaybackTime, clip->DurationSeconds);
    } else {
        anim.PlaybackTime = min(anim.PlaybackTime, clip->DurationSeconds);
    }

    // Sample each bone channel — index + lerp, no binary search
    float frame_f = anim.PlaybackTime * clip->SampleRate;
    for (uint32_t i = 0; i < skeleton->BoneCount; ++i) {
        const BoneChannel& ch = clip->Channels[i];
        // Guard: malformed clips may have empty channels
        if (ch.PositionKeys.IsEmpty() || ch.RotationKeys.IsEmpty() || ch.ScaleKeys.IsEmpty()) {
            continue; // skip this bone; leaves pose[i] at previous value
        }
        uint32_t key_count = (uint32_t)ch.PositionKeys.Size();
        // Guard: key_count - 1u underflows to UINT32_MAX if count is 0.
        // The empty-channel check above this block should prevent reaching here,
        // but add a second guard for safety.
        if (key_count == 0) continue;  // skip malformed channel
        uint32_t fi    = Core::Maths::min((uint32_t)frame_f, key_count - 1u);
        float    alpha = frame_f - static_cast<float>(fi);
        uint32_t ni    = Core::Maths::min(fi + 1u, key_count - 1u);

        (*pose)[i].Position = lerp(ch.PositionKeys[fi], ch.PositionKeys[ni], alpha);   // Vec3f lerp — see §5
        (*pose)[i].Rotation = slerp(ch.RotationKeys[fi], ch.RotationKeys[ni], alpha);  // ZEngine Quaternion slerp
        (*pose)[i].Scale    = lerp(ch.ScaleKeys[fi], ch.ScaleKeys[ni], alpha);
    }
});
```

### 4.2 `SkinningUploadSystem`

Converts the local-space pose from `AnimatorComponent` into world-space bone matrices
and uploads them to the GPU buffer in `SkinningComponent`.

**Dependencies:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<AnimatorComponent>())
               | MaskBit(ComponentTypeOf<SkeletonComponent>())
               | MaskBit(ComponentTypeOf<SkinningComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<SkinningComponent>()),
}
```

No conflict with `AnimationSampleSystem` because their write masks don't overlap — they
can run in parallel if no `OrderBefore` is declared. However, `SkinningUploadSystem`
reads the pose written by `AnimationSampleSystem`, so an ordering edge is required:

```cpp
world.OrderBefore(AnimationSampleSystem, SkinningUploadSystem);
```

**Algorithm:**

```
ForEach<SkeletonComponent, AnimatorComponent, SkinningComponent>(scene,
    [&anim_mgr, &render_resource_mgr](EntityID, SkeletonComponent& sk,
                                      AnimatorComponent& anim, SkinningComponent& skin) {
    if (anim.PoseHandle == Animation::INVALID_POSE) return;

    const SkeletonData* skeleton = anim_mgr.GetSkeleton(sk.Handle);
    auto* pose = anim_mgr.GetPose(anim.PoseHandle);
    if (!skeleton || !pose) return;

    // Scratch buffers: thread_local avoids per-entity allocation.
    // Each worker thread owns its own buffer — no contention, no allocation per call.
    static thread_local Core::Containers::Array<Core::Maths::Mat4f> global_pose;
    static thread_local Core::Containers::Array<Core::Maths::Mat4f> bone_matrices;
    global_pose.Resize(skeleton->BoneCount);
    bone_matrices.Resize(skeleton->BoneCount);

    // Forward pass: local → global (parent always precedes child — guaranteed by ExtractSkeleton)
    for (uint32_t i = 0; i < skeleton->BoneCount; ++i) {
        Mat4f local    = TRS((*pose)[i].Position, (*pose)[i].Rotation, (*pose)[i].Scale);  // see §5
        int32_t parent = skeleton->ParentIndices[i];
        global_pose[i] = (parent < 0) ? local : global_pose[parent] * local;
    }

    // Final skinning matrix: GlobalPose[i] * InverseBindMatrix[i]
    for (uint32_t i = 0; i < skeleton->BoneCount; ++i) {
        bone_matrices[i] = global_pose[i] * skeleton->InverseBindMatrices[i];
    }

    // Upload to GPU
    render_resource_mgr.UploadBuffer(
        skin.BoneMatrixBufferHandle,
        bone_matrices.Data(),
        skeleton->BoneCount * sizeof(Core::Maths::Mat4f));
});
```

`TRS(position, rotation, scale)` — see Section 5 (math prerequisites).

---

## 5. AssimpImporter — Skeleton and Clip Extraction

`AssimpImporter` currently has no animation extraction. Two new private methods are added:

```cpp
// In AssimpImporter.h — add to private section:
void ExtractSkeleton(const aiScene*, uuids::uuid_random_generator&, Animation::SkeletonData&);
void ExtractAnimationClips(const aiScene*, const Animation::SkeletonData&,
                           uuids::uuid_random_generator&,
                           Core::Containers::Array<Animation::AnimationClip>&);
```

### 5.1 `ExtractSkeleton`

Assimp stores inverse bind matrices per mesh (`aiMesh::mBones[i]->mOffsetMatrix`) but
stores the bone hierarchy in the scene node graph (`aiScene::mRootNode`). Both sources
are needed.

1. Walk all meshes to collect the union of all bone names into a `std::unordered_set<std::string>`.
2. Walk `aiScene::mRootNode` recursively (BFS). For each node whose name is in the bone
   name set, record it as a bone. This guarantees parents always appear before children
   in the resulting flat list.
3. Fill `SkeletonData::ParentIndices[i]` by looking up the node's parent name in the flat
   bone list. If the parent node is not itself a bone (e.g. a scene root), record `-1`.
4. Fill `SkeletonData::InverseBindMatrices[i]` from `aiBone::mOffsetMatrix` for the
   matching bone name, converting via the existing `ConvertToMat4`. Bones present in the
   hierarchy but absent from `mBones` (no mesh skinned to them) get an identity matrix.

### 5.2 `ExtractAnimationClips`

For each `aiAnimation` in the scene:

1. Create one `AnimationClip` with `DurationSeconds = anim->mDuration / anim->mTicksPerSecond`
   and `SampleRate = 30.f` (resample all channels to uniform 30 fps).
2. For each bone in the skeleton, find the matching `aiNodeAnim` channel by name.
3. Resample the Assimp keyframes (which use non-uniform timestamps) to uniform 30 fps
   by evaluating the Assimp interpolation at `t = frame / 30.f` for each frame index.
4. Store in `BoneChannel::PositionKeys`, `RotationKeys`, `ScaleKeys`.

Resampling to uniform rate at import time means the runtime sampler needs no binary
search — a simple index + lerp is enough every frame.

### 5.3 Integration into `ImportAsync`

After the existing mesh/material/texture extraction calls, add:

```cpp
Animation::SkeletonData skeleton;
ExtractSkeleton(ai_scene, uuid_gen, skeleton);

Core::Containers::Array<Animation::AnimationClip> clips;
ExtractAnimationClips(ai_scene, skeleton, uuid_gen, clips);

// Register with AnimationManager (passed via config or global accessor)
config.AnimationManager->AddSkeleton(std::move(skeleton));
for (auto& clip : clips) {
    config.AnimationManager->AddClip(std::move(clip));
}
```

`ImportConfiguration` needs an `AnimationManager*` field added.

---

## 5.4 Math Prerequisites

Two math helpers do not exist in the codebase and must be added to `ZEngine/Core/Maths/Matrix.h`
before the animation systems can compile.

**`Vec3f lerp`** — used by `AnimationSampleSystem` for position and scale interpolation:

```cpp
template<typename T>
inline Vec3<T> lerp(const Vec3<T>& a, const Vec3<T>& b, T t) {
    return a * (T(1) - t) + b * t;
}
```

**`TRS`** — used by `SkinningUploadSystem` to build a local bone matrix from
position, rotation, and scale. Uses the existing `quaternionToMat4`:

```cpp
inline Core::Maths::Mat4f TRS(
    const Core::Maths::Vec3f&             position,
    const Core::Maths::Quaternion<float>& rotation,
    const Core::Maths::Vec3f&             scale)
{
    Core::Maths::Mat4f m = quaternionToMat4(rotation);
    // apply scale to rotation columns
    m[0][0] *= scale.x; m[1][0] *= scale.x; m[2][0] *= scale.x;
    m[0][1] *= scale.y; m[1][1] *= scale.y; m[2][1] *= scale.y;
    m[0][2] *= scale.z; m[1][2] *= scale.z; m[2][2] *= scale.z;
    // apply translation
    m[0][3] = position.x;
    m[1][3] = position.y;
    m[2][3] = position.z;
    return m;
}
```

Both must be added before implementing any animation system code.

---

## 6. Scheduler Registration

```cpp
// App startup — after ECS system registrations in system-scheduler.md

SystemID anim_sample_id = world.RegisterSystem(AnimationSampleSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<SkeletonComponent>())
               | MaskBit(ComponentTypeOf<AnimatorComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<AnimatorComponent>()),
});

SystemID skinning_id = world.RegisterSystem(SkinningUploadSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<AnimatorComponent>())
               | MaskBit(ComponentTypeOf<SkeletonComponent>())
               | MaskBit(ComponentTypeOf<SkinningComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<SkinningComponent>()),
});

// AnimationSampleSystem writes AnimatorComponent, SkinningUploadSystem reads it → conflict
world.OrderBefore(anim_sample_id, skinning_id);

world.Commit();
```

Resulting wave layout (assuming no other system conflicts with these two):

```
Wave 0: AnimationSampleSystem (+ any other independent systems)
Wave 1: SkinningUploadSystem  (+ any other systems that only depend on Wave 0)
```

---

## 7. Editor Animation Preview (Option A)

In Edit mode `AnimationSampleSystem` and `SkinningUploadSystem` are NOT registered —
they are game systems added only when Play is pressed. Without a preview system,
animated characters appear frozen in their bind pose (T-pose) in the editor.

Option A: a dedicated `EditorAnimationPreviewSystem` registered only in Edit mode,
driven by an inspector-controlled `AnimatorComponent::PreviewActive` flag rather than
game logic.

### 7.1 `PreviewActive` flag

Add one field to `AnimatorComponent`:

```cpp
struct AnimatorComponent {
    Animation::AnimationClipHandle  ClipHandle      = Animation::INVALID_CLIP;
    Animation::PoseHandle           PoseHandle      = Animation::INVALID_POSE;
    float                           PlaybackTime    = 0.f;
    float                           PlaybackRate    = 1.f;
    bool                            Loop            = true;
    bool                            Playing         = false;
    bool                            AllocateFailed  = false;
    bool                            PreviewActive   = false;  // editor-only: drives preview
};
```

`PreviewActive` is set by the inspector's Preview button. It is never set by game code.
It is ignored by `AnimationSampleSystem` (which checks `Playing` in Play mode). It is
reset to false on Stop (when the scene snapshot is restored).

### 7.2 `EditorAnimationPreviewSystem`

Registered in Edit and Paused modes only. Uses the same sampling and upload logic as
the Play-mode systems but is driven by `PreviewActive` and advances time based on
wall-clock `raw_dt` (not the fixed-step accumulator — so it runs smoothly even when
the game loop is paused).

```cpp
// ZEngine/Editor/Animation/EditorAnimationPreviewSystem.h
#pragma once
#include <ECS/Scene.h>
#include <Animation/AnimationManager.h>
#include <Rendering/RenderResourceManager.h>

namespace ZEngine::Editor {

    // Registered in Edit and Paused modes.
    // SystemDeps:
    //   ReadMask:  SkeletonComponent | AnimatorComponent | SkinningComponent
    //   WriteMask: AnimatorComponent | SkinningComponent
    void EditorAnimationPreviewSystem(ECS::Scene& scene, float raw_dt,
                                      ECS::WorldCommands& cmds);

}  // namespace ZEngine::Editor
```

Tick logic (mirrors AnimationSampleSystem but checks `PreviewActive` not `Playing`):

```
ForEach<SkeletonComponent, AnimatorComponent, SkinningComponent>:
  if !anim.PreviewActive: return   ← skip entities not being previewed
  if anim.ClipHandle == INVALID_CLIP: return

  const AnimationClip* clip = anim_mgr.GetClip(anim.ClipHandle);
  if !clip: return

  // Allocate pose buffer on first preview frame
  if anim.PoseHandle == INVALID_POSE:
      anim.PoseHandle = anim_mgr.AllocatePose(skeleton->BoneCount);
      if anim.PoseHandle == INVALID_POSE: anim.AllocateFailed = true; return;

  // Advance time using raw_dt (not fixed step — preview runs at display rate)
  anim.PlaybackTime += raw_dt * anim.PlaybackRate;
  if anim.Loop:
      anim.PlaybackTime = fmod(anim.PlaybackTime, clip->DurationSeconds);
  else:
      anim.PlaybackTime = min(anim.PlaybackTime, clip->DurationSeconds);

  // Sample channels (identical algorithm to AnimationSampleSystem)
  // ... same index + lerp logic ...

  // Upload bone matrices to GPU (identical to SkinningUploadSystem)
  // ... TRS forward pass + InverseBindMatrix multiply + UploadBuffer ...
```

### 7.3 Inspector integration

The `InspectorViewUIComponent` adds a preview toolbar when an entity has
`AnimatorComponent`:

```
AnimatorComponent
  Clip:        [walk_cycle          ▼]
  Time:        [========|===] 1.2s / 3.4s
  Speed:       [1.0x]
  Loop:        [x]
  [▶ Preview]  [■ Stop]  [↩ Reset]
```

- **[▶ Preview]** — sets `anim.PreviewActive = true`, `anim.Playing = false`,
  `anim.PlaybackTime = 0`
- **[■ Stop]** — sets `anim.PreviewActive = false`, resets pose to bind pose
- **[↩ Reset]** — sets `anim.PlaybackTime = 0` without stopping
- The time scrubber slider writes directly to `anim.PlaybackTime` (preview
  shows the pose at that instant)

All inspector interactions go through `UndoRedoStack` as `ComponentFieldChangeCommand`
so they are undoable (see `editor-undo-redo.md`).

### 7.4 Scheduler registration

```cpp
// In Tetragrama editor system registration (OnInitialized, Edit mode only):

SystemID preview_id = world.RegisterSystem(
    Editor::EditorAnimationPreviewSystem, {
        .ReadMask  = MaskBit(ComponentTypeOf<SkeletonComponent>())
                   | MaskBit(ComponentTypeOf<AnimatorComponent>())
                   | MaskBit(ComponentTypeOf<SkinningComponent>()),
        .WriteMask = MaskBit(ComponentTypeOf<AnimatorComponent>())
                   | MaskBit(ComponentTypeOf<SkinningComponent>()),
    });

// Preview must run after any editor systems that write transforms,
// before render systems read skinning buffers.
world.OrderBefore(preview_id, skinning_upload_id);
```

When Play is pressed (`EditorPlayModeSystem::EnterPlay`), the editor unregisters
`EditorAnimationPreviewSystem` and registers the game's `AnimationSampleSystem` and
`SkinningUploadSystem` instead. When Stop is pressed, the reverse happens.

---

## 8. File Layout

```
ZEngine/
  Animation/
    AnimationHandles.h          (SkeletonHandle, AnimationClipHandle, PoseHandle)
    SkeletonData.h
    AnimationClip.h
    AnimationManager.h
    AnimationManager.cpp
    AnimationSampleSystem.h     (Play mode — registered on Play, removed on Stop)
    AnimationSampleSystem.cpp
    SkinningUploadSystem.h      (Play mode — same lifecycle)
    SkinningUploadSystem.cpp

  Editor/
    Animation/
      EditorAnimationPreviewSystem.h   (Edit/Paused modes — ZENGINE_EDITOR guard)
      EditorAnimationPreviewSystem.cpp

  ECS/
    Components/
      SkeletonComponent.h       (includes AnimationHandles.h only)
      AnimatorComponent.h       (includes AnimationHandles.h only; PreviewActive field)
      SkinningComponent.h

  Core/
    Maths/
      Matrix.h                  (add Vec3<T> lerp and TRS helper)

ZEngine/Importers/
  AssimpImporter.h              (add ExtractSkeleton, ExtractAnimationClips declarations)
  AssimpImporter.cpp            (implement both methods, wire into ImportAsync)
```

---

## 8. Dependencies

| Dependency | Why |
|---|---|
| `actor-ecs-architecture.md` | ECS::Scene, ComponentStorage, EntityID |
| `system-scheduler.md` | WorldTick registration, OrderBefore |
| VFS Ticket 1 (`vfs-design.md`) | AssimpImporter needs VFSPath for file loading |
| `import-pipeline.md` | ImportConfiguration, importer lifecycle |
| `render-resource-manager.md` | `UploadBuffer` for bone matrix GPU upload |

VFS Ticket 1 and the import pipeline must be implemented before `AssimpImporter` extraction
is wired end-to-end. The ECS components and systems can be written and unit-tested
independently using mock data.

---

## 9. Deliverables Checklist

- [ ] `ZEngine/Animation/AnimationHandles.h` — shared handle types, no includes
- [ ] `ZEngine/Animation/SkeletonData.h`
- [ ] `ZEngine/Animation/AnimationClip.h`
- [ ] `ZEngine/Animation/AnimationManager.h` + `.cpp` — skeleton pool, clip pool, pose buffer pool
- [ ] `ZEngine/Animation/AnimationSampleSystem.h` + `.cpp`
- [ ] `ZEngine/Animation/SkinningUploadSystem.h` + `.cpp`
- [ ] `ZEngine/ECS/Components/SkeletonComponent.h` — includes `AnimationHandles.h` only
- [ ] `ZEngine/ECS/Components/AnimatorComponent.h` — includes `AnimationHandles.h` only
- [ ] `ZEngine/ECS/Components/SkinningComponent.h`
- [ ] `ZEngine/Core/Maths/Matrix.h` — add `Vec3<T> lerp` and `TRS` helper
- [ ] `AssimpImporter::ExtractSkeleton` — hierarchy from `mRootNode`, bind matrices from `mBones`
- [ ] `AssimpImporter::ExtractAnimationClips` — uniform resample to 30 fps
- [ ] `ImportConfiguration` — add `AnimationManager*` field
- [ ] `world.RegisterSystem` returns `SystemID`; `world.OrderBefore(anim_sample_id, skinning_id)`
- [ ] `AnimatorComponent` — add `PreviewActive` bool field
- [ ] `EditorAnimationPreviewSystem.h/.cpp` — Edit/Paused mode preview, driven by `PreviewActive`, advances at `raw_dt`
- [ ] Inspector animation toolbar — Clip picker, time scrubber slider, Preview/Stop/Reset buttons
- [ ] `EditorPlayModeSystem::EnterPlay` — unregisters preview system, registers game sample+upload systems
- [ ] `EditorPlayModeSystem::EnterStop` — unregisters game systems, re-registers preview system
- [ ] `world.OrderBefore(preview_id, skinning_upload_id)` in editor system registration
- [ ] `tests/Animation/AnimationTest.cpp`:
  - [ ] Sample at t=0 returns first keyframe values
  - [ ] Sample at t=duration returns last keyframe values
  - [ ] Loop wraps playback time correctly
  - [ ] `AnimationSampleSystem` writes non-identity pose after one tick with a loaded clip
  - [ ] `SkinningUploadSystem` calls `UploadBuffer` with correct bone count
  - [ ] Stale `PoseHandle` (INVALID_POSE) causes early return, no crash

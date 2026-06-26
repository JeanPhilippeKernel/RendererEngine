# ZEngine — Animation Blend Trees and State Machine

**Priority:** Next-year plan — required for fluid character animation
**Status:** Design
**Depends on:** `animation-system.md` (v1 skeletal animation must be complete first)

---

## 1. Limitation of v1

The v1 animation system supports one `AnimationClip` per entity at a time. The entity has a single `ClipHandle` and a `PlaybackTime`. Transitioning from one animation to another — walking to running, idle to jumping — means instantly swapping the `ClipHandle`. The skeleton teleports between poses in a single frame, which is visually unacceptable.

Problems with the v1 approach at scale:

- Characters snap between animations with no transition smoothing.
- Blended locomotion (walk-run-sprint parameterized by speed) requires manual lerp code in game systems, duplicated per character type.
- There is no structured way to express "when grounded and moving, blend walk and run clips based on speed; when in air, play jump clip."

The blend tree system addresses all of these without changing the v1 `AnimationSampleSystem` for simple entities that do not need blending.

---

## 2. Blend Tree Concept

A blend tree is an acyclic directed tree of nodes. Each frame, the tree is evaluated bottom-up to produce a single output pose (an array of bone transforms). Leaf nodes sample an `AnimationClip` at a given time. Intermediate nodes blend two or more child poses weighted by a float parameter.

The tree is flat in memory — all nodes live in a `Array<BlendNode>` with integer child indices. There is no heap allocation per node, no virtual dispatch, no std::function. Tree evaluation is an iterative DFS over the index array.

Game code controls the tree by writing parameter values into a `BlendParameters` component on the entity. The tree reads those parameters each frame automatically.

---

## 3. Node Types

```cpp
// ZEngine/Animation/BlendTree/BlendTree.h

enum class BlendNodeType : uint8_t {
    Clip,           // leaf: samples a single AnimationClip at time t
    Blend1D,        // blends two children by a float parameter [0,1]
    Blend2D,        // blends four children by a Vec2f parameter (2D movement space)
    Additive,       // adds a partial pose on top of a base pose (e.g. aim overlay)
    StateMachine,   // routes evaluation to one of N child nodes based on active state
};

struct BlendNode {
    BlendNodeType Type        = BlendNodeType::Clip;
    uint32_t      LeftChild   = UINT32_MAX;  // index into BlendTree::Nodes (UINT32_MAX = none)
    uint32_t      RightChild  = UINT32_MAX;
    uint32_t      ClipHandle  = UINT32_MAX;  // Clip nodes only: index into clip pool
    uint32_t      ParamIndex  = UINT32_MAX;  // index into BlendParameters for weight/position
    float         Weight      = 0.f;         // Blend1D: static weight override if ParamIndex == UINT32_MAX
    Vec2f         Position    = {};          // Blend2D: static position override if ParamIndex == UINT32_MAX
};

struct BlendTree {
    Array<BlendNode> Nodes;
    uint32_t         RootIndex = 0;
};
```

Node semantics:

- **Clip**: `LeftChild` and `RightChild` are both `UINT32_MAX`. `ClipHandle` identifies the clip. The node samples the clip at the tree's current playback time (each Clip node can have its own speed multiplier — add `float SpeedScale = 1.f` as a later iteration).
- **Blend1D**: Has exactly `LeftChild` and `RightChild`. Evaluates both children, then blends: `pose = lerp(left_pose, right_pose, weight)`. `weight` comes from `BlendParameters` if `ParamIndex != UINT32_MAX`, otherwise from the static `Weight` field.
- **Blend2D**: Blends four children arranged at the corners of a unit square. The 2D position parameter selects bilinear blending weights. Useful for directional locomotion (forward/backward × left/right).
- **Additive**: Evaluates `LeftChild` as the base pose and `RightChild` as the additive delta. Adds joint rotations from the delta onto the base. Used for aim overlays, facial expressions, or injury limps layered on top of locomotion.
- **StateMachine**: Special case described in section 7.

---

## 4. BlendTreeComponent

Entities with complex animation replace their simple `AnimationClipComponent` with `BlendTreeComponent`:

```cpp
// ZEngine/Animation/BlendTree/BlendTreeComponent.h

struct BlendTreeComponent {
    uint32_t  TreeHandle   = UINT32_MAX;  // index into BlendTreeManager::m_Trees
    uint32_t  PoseHandle   = UINT32_MAX;  // index into AnimationManager pose buffer (output)
    float     PlaybackTime = 0.f;         // master clock advanced each frame
    float     PlaybackRate = 1.f;         // global speed multiplier
    bool      Playing      = true;
    bool      Looping      = true;
};
```

`PoseHandle` is allocated from the same pose buffer pool that `AnimationSampleSystem` uses for v1 entities. Both v1 and blend-tree entities write into the same buffer format — downstream systems (skinning, IK) are unaffected.

Entities with `BlendTreeComponent` are skipped by the v1 `AnimationSampleSystem` and instead processed by `BlendTreeSampleSystem`.

---

## 5. BlendTreeManager

`BlendTreeManager` owns the pool of `BlendTree` structures. It is a scene-level service (accessed as a singleton component on the scene, like `RenderResourceManager`).

```cpp
// ZEngine/Animation/BlendTree/BlendTreeManager.h

class BlendTreeManager {
public:
    // Allocate a new empty tree; returns a stable handle
    uint32_t CreateTree();

    // Access tree for editing (game code calls these at setup time)
    BlendTree&       GetTree(uint32_t handle);
    const BlendTree& GetTree(uint32_t handle) const;

    // Parameter writes (safe to call per-frame; these are the only hot-path setters)
    void SetWeight  (uint32_t tree_handle, uint32_t node_index, float weight);
    void SetPosition(uint32_t tree_handle, uint32_t node_index, Vec2f position);

    void DestroyTree(uint32_t handle);

private:
    Array<BlendTree> m_Trees;
    Array<uint32_t>  m_FreeList;
};
```

The `Array<BlendTree>` is allocated from a persistent arena (scene lifetime). `BlendNode` arrays inside each `BlendTree` are also arena-allocated. `DestroyTree` returns the slot to the free list but does not release memory (arena semantics).

---

## 6. BlendTreeSampleSystem

`BlendTreeSampleSystem` replaces `AnimationSampleSystem` for entities with `BlendTreeComponent`. It runs after physics and before skinning in the `WorldTick` schedule.

```cpp
// ZEngine/Animation/BlendTree/BlendTreeSampleSystem.h

class BlendTreeSampleSystem {
public:
    void Initialize(Scene& scene, BlendTreeManager& manager);
    void Tick(Scene& scene, float dt);
    SystemDeps GetDeps() const;
};
```

Tick logic:

1. For each entity with `BlendTreeComponent`:
   - Advance `PlaybackTime += dt * PlaybackRate` (if `Playing`).
   - Resolve `BlendParameters` component if present.
   - Call `EvaluateTree(tree, root_index, parameters, playback_time, out_pose)`.
2. Write the resulting pose into the entity's `PoseHandle` slot in the pose buffer.

### Tree Evaluation — Iterative DFS

The tree is evaluated with an explicit stack allocated from the frame arena (no recursive calls, no heap). Post-order traversal: a node is evaluated after both its children.

```
stack = [RootIndex]
eval_stack = []   // for bottom-up evaluation

// Phase 1: push all nodes in pre-order
while stack is not empty:
    node_idx = stack.pop()
    eval_stack.push(node_idx)
    if node has LeftChild:  stack.push(LeftChild)
    if node has RightChild: stack.push(RightChild)

// Phase 2: evaluate in reverse (post-order)
// scratch_mem: single contiguous frame-arena allocation covering all node poses

// BEFORE traversal begins, initialize all scratch slots to the identity pose.
// This prevents blend nodes from reading uninitialized data if a child node
// is skipped (e.g., due to a StateMachine branch or Selector short-circuit).

// Batch-allocate a single contiguous block for all node scratch poses.
// This is ONE allocation instead of tree.Nodes.Size() allocations,
// preventing arena fragmentation for trees with many nodes.
const uint32_t node_count  = tree.Nodes.Size();
const uint32_t bone_count  = skeleton->BoneCount;
const size_t   total_bytes = node_count * bone_count * sizeof(BoneTransform);

BoneTransform* scratch_mem = static_cast<BoneTransform*>(
    arena->Allocate(total_bytes, alignof(BoneTransform)));

// Zero-initialize to identity pose (Position=0, Rotation=identity, Scale=1).
// Zero-init gives Position=0 and Rotation={0,0,0,0}; fix Scale and Rotation.w:
memset(scratch_mem, 0, total_bytes);
for (uint32_t n = 0; n < node_count; ++n) {
    for (uint32_t b = 0; b < bone_count; ++b) {
        BoneTransform& bt = scratch_mem[n * bone_count + b];
        bt.Scale    = Core::Maths::Vec3f{1.f, 1.f, 1.f};
        bt.Rotation.w = 1.f;  // identity quaternion
    }
}

// pose_scratch[n] now refers to: scratch_mem + n * bone_count
// (a span of bone_count BoneTransforms for node n)

// IdentityPose: all bones at position (0,0,0), rotation identity, scale (1,1,1).
// Used as a safe default for unvisited blend tree nodes.

while eval_stack is not empty:
    node_idx = eval_stack.pop()
    node = tree.Nodes[node_idx]
    switch node.Type:
        Clip:
            sample AnimationClip[node.ClipHandle] at playback_time
            -> pose_scratch[node_idx]
        Blend1D:
            w = resolve_weight(node, parameters)
            pose_scratch[node_idx] = lerp(pose_scratch[node.LeftChild],
                                          pose_scratch[node.RightChild], w)
        Blend2D:
            p = resolve_position(node, parameters)
            pose_scratch[node_idx] = bilinear_blend(four_children, p)
        Additive:
            pose_scratch[node_idx] = additive_blend(pose_scratch[node.LeftChild],
                                                     pose_scratch[node.RightChild])
        StateMachine:
            active = state_machine_active_child(node, entity)
            pose_scratch[node_idx] = pose_scratch[active]

output = pose_scratch[RootIndex]
```

Pose scratch buffers are allocated from the frame arena before the loop and freed at end-of-frame. No per-frame heap allocation.

Blend operations use the existing `slerp` / `lerp` infrastructure from the v1 animation system. Rotations blend via `slerp`, translations and scales via `lerp`.

SystemDeps mask:

```cpp
SystemDeps BlendTreeSampleSystem::GetDeps() const {
    SystemDeps d;
    d.ReadComponents  = ComponentMask::BlendTreeComponent
                      | ComponentMask::BlendParametersComponent;
    d.WriteComponents = ComponentMask::PoseBufferComponent;
    return d;
}
```

---

## 7. Animation State Machine

The `StateMachine` blend node type implements a finite state machine inside the blend tree. It is the primary mechanism for expressing "play idle until the jump trigger fires, then play jump until grounded, then transition to land."

Data structures:

```cpp
// ZEngine/Animation/BlendTree/StateMachine.h

struct Transition {
    uint32_t    TriggerHash;       // StringHash of the trigger name
    uint32_t    TargetStateIndex;  // index into StateMachineData::States
    float       BlendDuration;     // seconds for cross-fade
};

struct AnimationState {
    uint32_t          ClipHandle;          // the clip played in this state
    Array<Transition> Transitions;
    float             SpeedScale = 1.f;
};

struct StateMachineData {
    Array<AnimationState> States;
    uint32_t              InitialStateIndex = 0;
};
```

`StateMachineData` is stored in the `BlendTree` node (a `BlendNode` of type `StateMachine` stores an index into a `BlendTreeManager`-owned pool of `StateMachineData`). This keeps `BlendNode` a fixed-size struct.

Per-entity runtime state is stored as a separate component:

```cpp
struct StateMachineInstanceComponent {
    uint32_t TreeHandle        = UINT32_MAX;
    uint32_t NodeIndex         = UINT32_MAX;    // which StateMachine node
    uint32_t CurrentStateIndex = 0;
    uint32_t PrevStateIndex    = UINT32_MAX;    // UINT32_MAX = no transition active
    float    TransitionTime    = 0.f;           // elapsed seconds into current transition
    float    TransitionDuration = 0.f;
};
```

Game code triggers transitions:

```cpp
// ZEngine/Animation/BlendTree/BlendTreeManager.h

// Called by game code (e.g. CharacterControllerSystem):
void TriggerTransition(Scene& scene, EntityID entity, uint32_t trigger_hash);
```

`TriggerTransition` looks up the entity's `StateMachineInstanceComponent`, finds the matching `Transition` in the current state's transition list by `TriggerHash`, and begins the cross-fade by setting `PrevStateIndex`, `TransitionTime = 0`, and `TransitionDuration`.

---

## 8. Cross-Fade Blending

When a state machine transition is active (`PrevStateIndex != UINT32_MAX`), `BlendTreeSampleSystem` evaluates both the previous and next states and blends between them:

```
alpha = TransitionTime / TransitionDuration   // [0, 1]
pose  = lerp(prev_state_pose, next_state_pose, alpha)
TransitionTime += dt
if TransitionTime >= TransitionDuration:
    PrevStateIndex = UINT32_MAX  // transition complete
```

Bone rotations blend with `slerp`. The cross-fade uses the same `slerp`/`lerp` utilities used elsewhere in the animation system. No new blend infrastructure is required.

The cross-fade is implemented inside the `StateMachine` node's evaluation branch in `BlendTreeSampleSystem`. It reuses the two pose scratch buffers already allocated per node level.

---

## 9. Blend Parameters

Game systems write float and bool parameters to the `BlendParametersComponent` on each animated entity. The blend tree reads them by index or by hash during evaluation.

```cpp
// ZEngine/Animation/BlendTree/BlendParameters.h

struct BlendParametersComponent {
    // Indexed access (fast path): game code knows the parameter index at setup time
    Array<float>  Floats;
    Array<bool>   Bools;

    // Named access (setup time only): map from hash to index
    UnorderedHashMap<uint32_t, uint32_t> FloatNameToIndex;
    UnorderedHashMap<uint32_t, uint32_t> BoolNameToIndex;
};
```

Game systems write parameters using the fast index path (resolved once at entity creation time):

```cpp
// At setup time:
uint32_t speed_index = params.FloatNameToIndex.Get(StringHash("Speed"));

// Each frame:
params.Floats[speed_index] = character_speed;
```

`Blend1D` and `Blend2D` nodes hold a `ParamIndex` that indexes directly into `BlendParametersComponent::Floats` or `::Floats` (for the x/y components of `Blend2D`). This avoids any hash lookup at evaluation time.

Trigger parameters (for state machine transitions) are fired via `TriggerTransition` directly rather than stored as persistent parameter values. This avoids the "trigger fires every frame until consumed" bug common in Unity-style animator controllers.

---

## 10. File Layout

```
ZEngine/Animation/BlendTree/
    BlendTree.h                     -- BlendNode, BlendNodeType, BlendTree structs
    BlendTreeComponent.h            -- BlendTreeComponent struct
    BlendTreeManager.h              -- BlendTreeManager class declaration
    BlendTreeManager.cpp            -- pool management, SetWeight/SetPosition
    BlendTreeSampleSystem.h         -- BlendTreeSampleSystem class declaration
    BlendTreeSampleSystem.cpp       -- iterative DFS evaluation, frame-arena scratch
    BlendParameters.h               -- BlendParametersComponent struct
    StateMachine.h                  -- Transition, AnimationState, StateMachineData
    StateMachineInstance.h          -- StateMachineInstanceComponent struct
```

---

## 11. Deliverables Checklist

- [ ] `BlendNode`, `BlendNodeType`, `BlendTree` structs defined
- [ ] `BlendTreeComponent` defined and registered in ECS; `AnimationSampleSystem` skips entities with it
- [ ] `BlendTreeManager` implemented: `CreateTree`, `GetTree`, `SetWeight`, `SetPosition`, `DestroyTree`
- [ ] `BlendParametersComponent` defined with fast float/bool index arrays and name-to-index maps
- [ ] `BlendTreeSampleSystem::Tick` implemented with iterative DFS and frame-arena scratch buffers
- [ ] `Clip` node evaluation: samples `AnimationClip` at `playback_time`
- [ ] `Blend1D` node evaluation: `lerp` with float parameter
- [ ] `Blend2D` node evaluation: bilinear blend with `Vec2f` parameter
- [ ] `Additive` node evaluation: additive pose delta applied to base pose
- [ ] `StateMachine` node type with `AnimationState`, `Transition`, `StateMachineData`
- [ ] `StateMachineInstanceComponent` defined and registered in ECS
- [ ] `TriggerTransition` API implemented and callable from game systems
- [ ] Cross-fade blending on state machine transitions (slerp/lerp over `TransitionDuration`)
- [ ] Unit tests: Blend1D at weight=0, 0.5, 1.0 produces expected poses
- [ ] Unit tests: state machine trigger fires correct transition, alpha advances correctly
- [ ] Integration test: character entity transitions from idle to run with visible pose blending
- [ ] No heap allocation in `BlendTreeSampleSystem::Tick` (frame-arena verified via allocator stats)
- [ ] BlendTreeSampleSystem: scratch buffer initialized to identity at start of each EvaluateTree call
- [ ] Unit test: Blend1D node with unvisited right child reads identity pose, not garbage

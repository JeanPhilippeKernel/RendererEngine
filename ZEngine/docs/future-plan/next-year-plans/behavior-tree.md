# ZEngine — Behavior Tree

**Priority:** Next-year plan — required for NPC AI beyond simple state machines
**Status:** Design — extends scripting.md §8 (read that section first)
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `physics-system.md` (raycasting for perception)

---

## 1. When to Use Behavior Trees

Three primary AI authoring tools exist in ZEngine: behavior trees, state machines, and Lua scripts. Each has a different cost/expressiveness tradeoff. Using the wrong tool produces either over-engineered boilerplate or unmaintainable spaghetti.

| Criterion | Behavior Tree | State Machine | Lua Script |
|---|---|---|---|
| Complexity | High — hierarchical, parallel, conditional trees | Low — 2–5 discrete states | Medium — sequential, event-driven |
| Typical use case | Guard NPC: patrol, investigate, attack, retreat, flee | Door: closed, opening, open, closing | Cutscene NPC, quest giver, interactive trigger |
| Authoring difficulty | Medium — tree structure must be designed | Low — trivial to implement in C++ | Low — designer-accessible |
| Runtime cost | Low (flat array, no vtable, no allocation per tick) | Very low | Medium (~50ns per Lua call) |
| Parallel branches | Yes (Parallel node) | No | No (coroutines are sequential) |
| Reusable subtrees | Yes (shared TreeHandle) | No | No |
| Designer-editable | v2 (JSON/visual editor) | No | Yes |
| Long-running actions | Yes (Running status across ticks) | Manual (state persists) | Yes (coroutine yield) |
| Hierarchical interruption | Yes (Selector re-evaluates on each tick) | Manual | Manual |

**Rule of thumb:** if the NPC has more than five distinct behaviors that can preempt each other based on world state (visibility, threat level, health), use a behavior tree. If it has two or three states driven by a timer or animation event, use a state machine. If it is a scripted one-off sequence, use Lua.

---

## 2. Node Types

Behavior trees in ZEngine use a flat array of `BTNode` structs. There are no virtual functions, no heap allocations per tick, and no pointer indirection between nodes. The tree structure is encoded entirely in the `FirstChild` and `NextSibling` index fields, forming an intrusive linked tree stored in a contiguous array.

```cpp
enum class BTNodeType : uint8_t {
    Sequence,    // Composite: run children left-to-right; stop and return Failure on first child Failure
    Selector,    // Composite: run children left-to-right; stop and return Success on first child Success
    Parallel,    // Composite: run all children simultaneously; configurable success/failure policy
    Decorator,   // Unary: wraps one child; transforms its result according to DecoratorType
    Condition,   // Leaf: evaluates a BTConditionFn; returns Success or Failure immediately (no side effects)
    Action,      // Leaf: executes a BTActionFn; may return Running across multiple ticks
};

enum class BTStatus : uint8_t {
    Success,
    Failure,
    Running,
};

struct BTNode {
    BTNodeType  Type            = BTNodeType::Action;
    uint8_t     DecoratorType   = 0;       // Decorator nodes only: Invert=0, Repeat=1, Cooldown=2
    uint8_t     _pad[2]         = {};
    uint32_t    FirstChild      = UINT32_MAX;  // index of first child in the owning tree's node array
    uint32_t    NextSibling     = UINT32_MAX;  // index of next sibling (for composite iteration)
    uint32_t    ActionID        = 0;           // Action nodes: index into BTCallbackTable::Actions
                                               // Condition nodes: index into BTCallbackTable::Conditions
    float       DecoratorParam  = 0.f;         // Repeat: max repeat count; Cooldown: duration in seconds
};
```

**Composite traversal encoding:** `FirstChild` points to the leftmost child. Each child's `NextSibling` points to the next sibling. `UINT32_MAX` means "no child" or "no next sibling." This is equivalent to a left-child / right-sibling binary representation, fully linearised in the array.

**No virtual dispatch:** The behavior for each `BTNodeType` is a switch-case in `BehaviorTreeSystem::EvaluateNode`. This is a single, predictable indirect branch per node evaluation — the type enum fits in a CPU register and the switch body is inlined.

---

## 3. Blackboard

The blackboard is a per-entity key-value store for AI working memory. It holds the data that behavior tree nodes read and write: target positions, threat levels, patrol waypoints, cooldown timers, entity references.

```cpp
struct Blackboard {
    UnorderedHashMap<uint32_t, float>    Floats;    // keyed by FNV-32 hash of string key
    UnorderedHashMap<uint32_t, bool>     Bools;
    UnorderedHashMap<uint32_t, Vec3f>    Vectors;
    UnorderedHashMap<uint32_t, EntityID> Entities;
};
```

Keys are `uint32_t` FNV-32 hashes of string names computed at startup. Runtime lookups do not touch strings — they use precomputed hash constants. Define key constants as:

```cpp
namespace BBKey {
    constexpr uint32_t TargetPosition  = 0x8B4C2F3A;  // FNV32("target_position")
    constexpr uint32_t Speed           = 0xF1A7E294;  // FNV32("speed")
    constexpr uint32_t PlayerEntity    = 0x3C9D0E17;  // FNV32("player_entity")
    constexpr uint32_t IsAlerted       = 0x7E25B841;  // FNV32("is_alerted")
}
```

Blackboard memory is owned by `BehaviorTreeComponent` (inline, not heap). `UnorderedHashMap` uses the arena allocator provided to the component at construction time — there are no `new` calls during tree evaluation.

---

## 4. `BehaviorTreeComponent`

```cpp
struct BehaviorTreeComponent {
    uint32_t TreeHandle       = UINT32_MAX;  // index into BehaviorTreeManager::m_trees
    uint32_t ActiveNodeIndex  = 0;           // index of the currently Running action node; 0 = root
    BTStatus LastStatus       = BTStatus::Success;
    Blackboard Board          = {};
};
```

`TreeHandle` refers to a tree definition in `BehaviorTreeManager`. Multiple entities can share the same `TreeHandle` — the tree definition (node array, callback table) is read-only shared data. Per-entity mutable state lives entirely in `BehaviorTreeComponent::Board` and `ActiveNodeIndex`.

`ActiveNodeIndex` stores the resume point for `Running` action nodes (see §7). When the tree is re-evaluated on the next tick and an action previously returned `Running`, the system checks whether it should resume at `ActiveNodeIndex` or re-evaluate from the root.

---

## 5. Action and Condition Callbacks

Callbacks are plain function pointers with no `std::function`, no lambda captures, no heap allocation. The `void* ctx` parameter provides access to engine systems (passed from `BehaviorTreeSystem::Tick`).

```cpp
using BTActionFn    = BTStatus (*)(void* ctx, EntityID entity, Blackboard& board, float dt);
using BTConditionFn = bool     (*)(void* ctx, EntityID entity, const Blackboard& board);

struct BTCallbackTable {
    Array<BTActionFn>    Actions;
    Array<BTConditionFn> Conditions;
    void*                Ctx = nullptr;  // typically a pointer to a game-side AIContext struct
};
```

The game registers callbacks in `ZGame_RegisterSystems` (the game-side initialization hook called before the first world tick):

```cpp
void ZGame_RegisterSystems(SystemRegistry& reg, BTCallbackTable& bt) {
    bt.Actions.Push(MoveToAction);        // ACTION_MOVE_TO     = 0
    bt.Actions.Push(WaitAction);          // ACTION_WAIT        = 1
    bt.Actions.Push(AttackAction);        // ACTION_ATTACK      = 2
    bt.Actions.Push(PatrolAction);        // ACTION_PATROL      = 3

    bt.Conditions.Push(IsPlayerVisible);  // COND_PLAYER_VISIBLE = 0
    bt.Conditions.Push(IsHealthLow);      // COND_HEALTH_LOW     = 1

    bt.Ctx = &g_ai_context;
}
```

Action and condition indices are defined as constants in the game header:

```cpp
constexpr uint32_t ACTION_MOVE_TO      = 0;
constexpr uint32_t ACTION_WAIT         = 1;
constexpr uint32_t ACTION_ATTACK       = 2;
constexpr uint32_t ACTION_PATROL       = 3;
constexpr uint32_t COND_PLAYER_VISIBLE = 0;
constexpr uint32_t COND_HEALTH_LOW     = 1;
```

`BTNode::ActionID` stores these indices. The system dispatches via `bt.Actions[node.ActionID](ctx, entity, board, dt)` — one array index and one indirect call per leaf node.

---

## 6. `BehaviorTreeSystem`

`BehaviorTreeSystem` is an ECS system. It iterates all entities with `BehaviorTreeComponent` and evaluates each entity's tree once per tick.

**SystemDeps:**

```cpp
SystemDeps BehaviorTreeSystem::GetDeps() {
    return SystemDeps{}
        .Reads<TransformComponent>()
        .Reads<PhysicsBodyComponent>()
        .Writes<BehaviorTreeComponent>()
        .After<PhysicsSystem>()    // perception conditions need current physics state
        .After<LuaSystem>()        // LuaSystem may modify transforms that AI reacts to
        .Before<AnimationSystem>() // AI decisions drive animation state
        .Before<RenderSystem>();
}
```

**Evaluation loop:**

```cpp
void BehaviorTreeSystem::Tick(WorldTick& tick) {
    ZENGINE_PROFILE_SCOPE("BehaviorTreeSystem");
    float dt = tick.DeltaSeconds;

    tick.Scene.ForEach<BehaviorTreeComponent>([&](EntityID id, BehaviorTreeComponent& btc) {
        if (btc.TreeHandle == UINT32_MAX) return;

        const BTTree& tree = m_manager.GetTree(btc.TreeHandle);
        btc.LastStatus = EvaluateNode(tree, 0 /*root*/, id, btc, dt);
    });
}
```

**`EvaluateNode` uses an explicit stack — no recursion:**

```cpp
static constexpr int32_t MAX_BT_DEPTH = 32;
static_assert(MAX_BT_DEPTH > 0 && MAX_BT_DEPTH <= 64,
    "MAX_BT_DEPTH must be between 1 and 64");

BTStatus BehaviorTreeSystem::EvaluateNode(
    const BTTree& tree, uint32_t node_idx,
    EntityID entity, BehaviorTreeComponent& btc, float dt)
{
    // Iterative traversal with an explicit stack stored in a fixed-size local array
    // Stack frames encode (node_index, child_status_so_far) to support composite resumption
    BTEvalFrame stack[MAX_BT_DEPTH];
    int32_t top = 0;

    stack[0] = { node_idx, BTStatus::Success, 0 /*child_cursor*/ };

    while (top >= 0) {
        BTEvalFrame& frame = stack[top];
        const BTNode& node = tree.Nodes[frame.NodeIdx];

        switch (node.Type) {
        case BTNodeType::Condition: {
            bool result = m_callbacks.Conditions[node.ActionID](
                m_callbacks.Ctx, entity, btc.Board);
            frame.Result = result ? BTStatus::Success : BTStatus::Failure;
            --top;
            break;
        }
        case BTNodeType::Action: {
            BTStatus s = m_callbacks.Actions[node.ActionID](
                m_callbacks.Ctx, entity, btc.Board, dt);
            if (s == BTStatus::Running) btc.ActiveNodeIndex = frame.NodeIdx;
            frame.Result = s;
            --top;
            break;
        }
        // ... Sequence, Selector, Parallel, Decorator cases
        // For all composite node types (Sequence, Selector, Parallel) that push
        // children onto the evaluation stack, a bounds check must precede every push:
        //
        //   ZENGINE_VALIDATE_ASSERT(top + 1 < MAX_BT_DEPTH,
        //       "BehaviorTree: stack overflow at node %u. "
        //       "Tree depth exceeds MAX_BT_DEPTH=%d. "
        //       "Possible infinite loop or excessively nested tree.",
        //       frame.NodeIdx, MAX_BT_DEPTH);
        //   // Write to the NEW slot index BEFORE incrementing top,
        //   // so the write index is validated before use.
        //   stack[top + 1] = { child_idx, BTStatus::Success, 0 };
        //   ++top;
        }
    }

    return stack[0].Result;
}
```

`MAX_BT_DEPTH` = 32. A debug assert fires if the tree exceeds this depth. This bounds the stack-frame array size to 32 × sizeof(BTEvalFrame) bytes on the C++ stack — no heap allocation during evaluation.

---

## 7. Execution Model

**Tick policy:** One full tree evaluation per entity per fixed-step tick. Variable-rate ticks (`WorldTick::DeltaSeconds`) are passed to action callbacks — it is the action's responsibility to use `dt` correctly for time-based behavior.

**Running actions:** When an action callback returns `BTStatus::Running`, it means the action needs more time (the NPC has not yet reached its destination, the attack animation is still playing, etc.). `BehaviorTreeComponent::ActiveNodeIndex` is set to that node's index.

On the **next tick**, `BehaviorTreeSystem` re-evaluates from the root. The Sequence or Selector parent that owns the Running action will re-evaluate its children left-to-right. If a Condition to the left of the Running action fails (a higher-priority threat appears), the Selector will switch to a different branch, naturally aborting the Running action. If all conditions leading to the Running action still hold, the action callback is invoked again with the new `dt`. The action continues its work.

This is the standard behavior tree preemption model. The `ActiveNodeIndex` field serves as a hint and for debugging, but the tree always re-evaluates from the root each tick. Actions must be stateless enough to resume correctly when called again — any persistent state should live in the `Blackboard`.

**Cooldown decorators:** The `Cooldown` decorator type stores the elapsed-since-last-success time in the Blackboard (keyed by node index). On tick, if the cooldown has not expired, the decorator returns `Failure` immediately without evaluating the child. The Selector above it will move on to the next branch. This prevents rapid re-entry into expensive actions (attack animations, search behavior).

---

## 8. Built-in Action Examples

These actions are implemented in `ZEngine/AI/BehaviorTree/BuiltinActions.cpp` and are available to all games.

### MoveToAction

Index: registered as `ACTION_BUILTIN_MOVE_TO`.

Reads `BBKey::TargetPosition` (`Vec3f`) and `BBKey::Speed` (`float`) from the blackboard. Moves the entity's `TransformComponent` toward the target at `Speed` units per second. Returns `BTStatus::Running` while the entity is more than 0.1 units from the target. Returns `BTStatus::Success` when within the threshold. Returns `BTStatus::Failure` if `TargetPosition` is not set in the blackboard.

```cpp
BTStatus MoveToAction(void* ctx, EntityID entity, Blackboard& board, float dt) {
    AIContext* ai = (AIContext*)ctx;

    Vec3f* target = board.Vectors.Find(BBKey::TargetPosition);
    float* speed  = board.Floats.Find(BBKey::Speed);
    if (!target || !speed) return BTStatus::Failure;

    TransformComponent& xform = ai->scene->GetComponent<TransformComponent>(entity);
    Vec3f dir = *target - xform.Position;
    float dist = dir.Length();
    if (dist < 0.1f) return BTStatus::Success;

    xform.Position = xform.Position + (dir / dist) * (*speed) * dt;
    xform.MarkDirty();
    return BTStatus::Running;
}
```

### WaitAction

Reads `DecoratorParam` from the node (set at tree-build time) as the wait duration in seconds. Stores elapsed time in `board.Floats` keyed by a per-node hash. Returns `Running` until elapsed >= duration, then clears the elapsed entry and returns `Success`.

### IsPlayerVisibleCondition

Index: registered as `COND_BUILTIN_PLAYER_VISIBLE`.

Reads `BBKey::PlayerEntity` from the blackboard to get the player's `EntityID`. Performs a physics raycast from the NPC's position to the player's position (via `PhysicsSystem::Raycast`). Returns true if the ray reaches the player without obstruction. Returns false if the player entity is invalid, not set in the blackboard, or the ray is blocked.

```cpp
bool IsPlayerVisibleCondition(void* ctx, EntityID entity, const Blackboard& board) {
    AIContext* ai = (AIContext*)ctx;

    const EntityID* player_id = board.Entities.Find(BBKey::PlayerEntity);
    if (!player_id || !player_id->IsValid()) return false;

    Vec3f npc_pos    = ai->scene->GetComponent<TransformComponent>(entity).Position;
    Vec3f player_pos = ai->scene->GetComponent<TransformComponent>(*player_id).Position;
    Vec3f dir        = (player_pos - npc_pos);
    float dist       = dir.Length();

    RaycastHit hit;
    bool blocked = ai->physics->Raycast(npc_pos, dir / dist, dist, hit);
    return !blocked;
}
```

---

## 9. Tree Authoring

**v1: C++ builder API.** Trees are defined in C++ using a `BehaviorTreeBuilder` and registered at game startup. The builder uses a stack to track the current parent node and a flat `Array<BTNode>` as the output.

```cpp
BehaviorTreeBuilder b;
uint32_t guard_tree = b
    .Selector()                                    // Try attack, else patrol
        .Sequence()                                // Attack sequence
            .Condition(COND_PLAYER_VISIBLE)
            .Action(ACTION_ATTACK)
        .End()
        .Sequence()                                // Patrol sequence
            .Action(ACTION_PATROL)
        .End()
    .End()
    .Build(arena);

uint32_t handle = behavior_tree_mgr.RegisterTree(guard_tree);
```

`BehaviorTreeBuilder::Build(ArenaAllocator*)` allocates the final `BTTree` (node array + metadata) from the provided arena. The builder itself uses a temporary stack allocated on the C++ call stack.

`BehaviorTreeManager::RegisterTree` stores the tree in a flat `Array<BTTree>` and returns a `uint32_t` handle. Entities are assigned handles via `BehaviorTreeComponent::TreeHandle`.

**v2 (deferred):** JSON-defined trees, loaded from the VFS, with a visual node editor in the engine editor. The node array representation is identical; only the authoring front-end changes.

---

## 10. File Layout

```
ZEngine/AI/BehaviorTree/
    BTDefs.h                   -- BTNodeType, BTStatus, BTNode, BTCallbackTable, Blackboard
    BTDefs.cpp                 -- (minimal; mostly inline / constexpr)
    BehaviorTreeComponent.h
    BehaviorTreeManager.h
    BehaviorTreeManager.cpp    -- RegisterTree, GetTree, BTTree storage
    BehaviorTreeSystem.h
    BehaviorTreeSystem.cpp     -- ECS system, EvaluateNode iterative traversal
    BehaviorTreeBuilder.h
    BehaviorTreeBuilder.cpp    -- C++ builder API
    BuiltinActions.h
    BuiltinActions.cpp         -- MoveToAction, WaitAction, IsPlayerVisibleCondition, etc.
```

No new third-party dependencies. `UnorderedHashMap` and `Array` are ZEngine custom containers.

---

## 11. Deliverables Checklist

- [ ] `BTDefs.h` — `BTNodeType`, `BTStatus`, `BTNode`, `Blackboard`, `BTCallbackTable`
- [ ] `BehaviorTreeComponent.h` — ECS component struct
- [ ] `BehaviorTreeManager.h` / `BehaviorTreeManager.cpp` — tree pool, `RegisterTree`, `GetTree`
- [ ] `BehaviorTreeSystem.h` / `BehaviorTreeSystem.cpp` — iterative `EvaluateNode`, `Tick`, SystemDeps
- [ ] `BehaviorTreeBuilder.h` / `BehaviorTreeBuilder.cpp` — C++ fluent builder
- [ ] `BuiltinActions.cpp` — `MoveToAction`, `WaitAction`, `IsPlayerVisibleCondition`
- [ ] Cooldown decorator implemented and tested
- [ ] Parallel node implemented with configurable success/failure policy
- [ ] Integration test: guard NPC transitions patrol -> investigate -> attack on player approach
- [ ] Preemption test: Running action correctly interrupted when Selector re-evaluates higher-priority branch
- [ ] Performance test: 200 entities with 20-node trees, measure BehaviorTreeSystem tick time against 500µs budget
- [ ] `MAX_BT_DEPTH` assert triggers correctly on artificially deep tree (debug build only)
- [ ] BehaviorTreeSystem: stack overflow assert fires before array write (not after)
- [ ] Unit test: create tree with depth 33; verify ZENGINE_VALIDATE_ASSERT fires at the 33rd level

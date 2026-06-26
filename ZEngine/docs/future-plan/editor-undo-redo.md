# ZEngine — Editor Undo / Redo

**Priority:** P1 — Required for Sprint 13; editor is unusable without it  
**Status:** Design  
**Depends on:** `actor-ecs-architecture.md`, `component-reflection.md`, `editor-entity-selection.md`, `editor-play-mode.md`  
**Blocks:** Tetragrama editor usability; every edit operation requires undo

---

## 1. Design Philosophy

Command pattern. Every editor mutation is wrapped in an `IEditorCommand` — a plain
data struct with function pointers for Execute and Undo (no virtual dispatch, DOD).
Commands are pushed onto a fixed-capacity undo stack. Undo moves the top command to
a redo stack. Redo moves it back.

**Key constraints:**
- Works **only in Edit mode**. Entering Play clears both stacks.
- No `std::function`. Plain function pointer + `void* data` (DOD rule).
- Commands are arena-allocated. Stack capacity: 100 commands. Oldest evicted when full.
- Undo/Redo mutates `ECS::Scene` directly and synchronously — not via `WorldCommands`.
- Compound commands: multiple mutations undo as a single step.
- Merge window: continuous edits (e.g. gizmo drag) merge into one command.

---

## 2. `IEditorCommand` — function table, no virtual

```cpp
// ZEngine/Editor/UndoRedo/EditorCommand.h
#pragma once
#include <ECS/Scene.h>
#include <cstdint>

namespace ZEngine::Editor {

    struct CommandContext {
        ECS::Scene*           Scene;
        EditorSelectionState* Selection;
        Core::Memory::ArenaAllocator* Arena;
    };

    using CommandExecuteFn = void (*)(void* data, const CommandContext& ctx);
    using CommandUndoFn    = void (*)(void* data, const CommandContext& ctx);
    using CommandDestroyFn = void (*)(void* data);         // arena cleanup; may be null
    using CommandNameFn    = const char* (*)(const void*); // "Move Entity", "Delete"...
    using CommandMergeFn   = bool (*)(void* existing, const void* incoming); // merge or not

    struct IEditorCommand {
        void*            Data    = nullptr;
        CommandExecuteFn Execute = nullptr;
        CommandUndoFn    Undo    = nullptr;
        CommandDestroyFn Destroy = nullptr;  // null if no cleanup needed
        CommandNameFn    Name    = nullptr;
        CommandMergeFn   Merge   = nullptr;  // null if not mergeable
        uint64_t         TimeNs  = 0;        // steady_clock time of push (for merge window)
    };

}  // namespace ZEngine::Editor
```

---

## 3. `UndoRedoStack`

Fixed-capacity ring buffer. Oldest commands evicted when full.

```cpp
// ZEngine/Editor/UndoRedo/UndoRedoStack.h
#pragma once
#include <Editor/UndoRedo/EditorCommand.h>
#include <Core/Memory/Allocator.h>

namespace ZEngine::Editor {

    class UndoRedoStack {
    public:
        static constexpr uint32_t kCapacity = 100;

        void Initialize(Core::Memory::ArenaAllocator* arena);

        // Execute the command and push onto the undo stack.
        // Clears the redo stack (new action invalidates redo branch).
        // If the top command has a Merge fn and can merge, merges instead of pushing.
        void Execute(IEditorCommand cmd, const CommandContext& ctx);

        // Undo the top command. Moves it to redo stack.
        void Undo(const CommandContext& ctx);

        // Redo the top undone command. Moves it back to undo stack.
        void Redo(const CommandContext& ctx);

        // Clear both stacks (called when entering Play mode).
        void Clear();

        [[nodiscard]] bool        CanUndo()   const { return m_undo_top > 0; }
        [[nodiscard]] bool        CanRedo()   const { return m_redo_top > 0; }
        [[nodiscard]] const char* UndoName()  const;  // "Undo Move Entity"
        [[nodiscard]] const char* RedoName()  const;  // "Redo Delete Entity"

    private:
        IEditorCommand m_undo[kCapacity] = {};
        IEditorCommand m_redo[kCapacity] = {};
        uint32_t       m_undo_top = 0;
        uint32_t       m_redo_top = 0;
        Core::Memory::ArenaAllocator* m_arena = nullptr;

        void EvictOldest();
    };

}  // namespace ZEngine::Editor
```

### 3.1 Execute implementation

```cpp
void UndoRedoStack::Execute(IEditorCommand cmd, const CommandContext& ctx) {
    cmd.TimeNs = std::chrono::steady_clock::now().time_since_epoch().count();

    // Attempt merge with the top command
    if (m_undo_top > 0 && cmd.Merge != nullptr) {
        IEditorCommand& top = m_undo[m_undo_top - 1];
        if (top.Merge != nullptr &&
            top.Execute == cmd.Execute &&        // same command type
            (cmd.TimeNs - top.TimeNs) < 200'000'000ULL)  // within 200ms
        {
            if (top.Merge(top.Data, cmd.Data)) {
                top.TimeNs = cmd.TimeNs;  // extend the merge window
                if (cmd.Destroy) cmd.Destroy(cmd.Data);
                return;  // merged — no new push
            }
        }
    }

    // Evict oldest if full
    if (m_undo_top >= kCapacity) EvictOldest();

    // Execute and push
    cmd.Execute(cmd.Data, ctx);
    m_undo[m_undo_top % kCapacity] = cmd;
    m_undo_top++;

    // Clear redo stack
    for (uint32_t i = 0; i < m_redo_top; ++i)
        if (m_redo[i].Destroy) m_redo[i].Destroy(m_redo[i].Data);
    m_redo_top = 0;
}
```

---

## 4. Command implementations

### 4.1 `TransformChangeCommand` — most common

Fired when the user finishes a gizmo drag. Supports merge so micro-movements consolidate.

```cpp
struct TransformChangeData {
    ECS::EntityID Entity;
    Core::Maths::Vec3f OldPosition, OldRotation, OldScale;
    Core::Maths::Vec3f NewPosition, NewRotation, NewScale;
};

// Execute: apply NewPosition/Rotation/Scale
static void TransformChange_Execute(void* data, const CommandContext& ctx) {
    auto* d = static_cast<TransformChangeData*>(data);
    auto* t = ctx.Scene->GetComponent<ECS::Components::TransformComponent>(d->Entity);
    if (!t) return;
    t->Position = d->NewPosition;
    t->Rotation = d->NewRotation;
    t->Scale    = d->NewScale;
}

// Undo: apply OldPosition/Rotation/Scale
static void TransformChange_Undo(void* data, const CommandContext& ctx) {
    auto* d = static_cast<TransformChangeData*>(data);
    auto* t = ctx.Scene->GetComponent<ECS::Components::TransformComponent>(d->Entity);
    if (!t) return;
    t->Position = d->OldPosition;
    t->Rotation = d->OldRotation;
    t->Scale    = d->OldScale;
}

// Merge: same entity → update NewPosition/Rotation/Scale in place
static bool TransformChange_Merge(void* existing, const void* incoming) {
    auto* e = static_cast<TransformChangeData*>(existing);
    auto* i = static_cast<const TransformChangeData*>(incoming);
    if (e->Entity != i->Entity) return false;
    e->NewPosition = i->NewPosition;
    e->NewRotation = i->NewRotation;
    e->NewScale    = i->NewScale;
    return true;
}

static const char* TransformChange_Name(const void* data) {
    return "Move Entity";
}

IEditorCommand MakeTransformChangeCommand(const TransformChangeData& d,
                                          Core::Memory::ArenaAllocator* arena) {
    auto* data = ZPLUGIN_ALLOC(arena, TransformChangeData);
    *data = d;
    return { data, TransformChange_Execute, TransformChange_Undo,
             nullptr, TransformChange_Name, TransformChange_Merge };
}
```

**How the inspector triggers this** — capture old value before edit, new value after:

```cpp
// InspectorViewUIComponent::DrawField (float case):
float old_val = *static_cast<float*>(field_ptr);
if (ImGui::DragFloat(field.Name, static_cast<float*>(field_ptr), 0.1f)) {
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        // User released the drag — commit a ComponentFieldChangeCommand
        float new_val = *static_cast<float*>(field_ptr);
        // restore old so Execute can write new
        *static_cast<float*>(field_ptr) = old_val;
        undo_stack->Execute(MakeFieldChangeCommand(..., old_val, new_val), ctx);
    }
}
```

---

### 4.2 `ComponentFieldChangeCommand` — inspector field edits

```cpp
struct ComponentFieldChangeData {
    ECS::EntityID        Entity;
    ECS::ComponentTypeID TypeID;
    uint32_t             FieldOffset;
    uint32_t             FieldSize;     // max 64 bytes per field
    uint8_t              OldValue[64];
    uint8_t              NewValue[64];
    const char*          FieldName;     // for display
};

// Execute: memcpy NewValue → component+FieldOffset
// Undo:    memcpy OldValue → component+FieldOffset
// Merge:   same Entity+TypeID+FieldOffset → update NewValue
// Name:    "Changed 'Speed'"
```

---

### 4.3 `CreateEntityCommand`

```cpp
struct CreateEntityData {
    ECS::EntityID CreatedEntity;                          // set during Execute
    Core::Containers::Array<uint8_t> ComponentSnapshot;   // binary dump of entity state
};

// Execute: restore entity from ComponentSnapshot (used on Redo after Undo-delete)
// Undo:    snapshot entity, then destroy it
// Name:    "Create Entity"

// First-time Execute (when user clicks "Add Entity"):
//   - Entity already exists in scene (created by WorldCommands before this cmd)
//   - Just record the EntityID for future Undo
// Subsequent Execute (Redo after Undo):
//   - Recreate entity from ComponentSnapshot
```

---

### 4.4 `DeleteEntityCommand`

```cpp
struct DeleteEntityData {
    ECS::EntityID DeletedEntity;
    Core::Containers::Array<uint8_t> ComponentSnapshot;
};

// Execute: snapshot entity, destroy it, update Selection if selected
// Undo:    recreate entity from snapshot

// Snapshot format (from component-reflection.md):
// [TypeID u32][Size u32][raw bytes] repeated for each component
static void SnapshotEntity(ECS::EntityID id, ECS::Scene& scene,
                            Core::Containers::Array<uint8_t>& out) {
    ECS::ComponentReflectionRegistry::Get().ForEach([&](const ECS::ComponentMeta& m) {
        void* data = scene.GetComponentRaw(id, m.TypeID);
        if (!data) return;
        out.Append((uint8_t*)&m.TypeID,  sizeof(uint32_t));
        out.Append((uint8_t*)&m.Size,    sizeof(uint32_t));
        out.Append((uint8_t*)data,       m.Size);
    });
}

// Restore format: read back type_id + size + bytes, call AddComponentRaw + memcpy
```

---

### 4.5 `AddComponentCommand`

```cpp
struct AddComponentData {
    ECS::EntityID        Entity;
    ECS::ComponentTypeID TypeID;
    uint32_t             Size;
    uint32_t             Align;
};
// Execute: Scene::AddComponentRaw(entity, typeID, size, align)
// Undo:    Scene::RemoveComponentRaw(entity, typeID)
// Name:    "Add <TypeName>"
```

---

### 4.6 `RemoveComponentCommand`

```cpp
struct RemoveComponentData {
    ECS::EntityID        Entity;
    ECS::ComponentTypeID TypeID;
    uint32_t             SnapshotSize;
    uint8_t              Snapshot[512]; // component data at removal; max 512 bytes
};
// Execute: snapshot component data, RemoveComponentRaw
// Undo:    AddComponentRaw + memcpy Snapshot back
// Name:    "Remove <TypeName>"
```

---

### 4.7 `RenameEntityCommand`

```cpp
struct RenameEntityData {
    ECS::EntityID Entity;
    char OldName[64];
    char NewName[64];
};
// Execute: NameComponent.Name = NewName
// Undo:    NameComponent.Name = OldName
// Merge:   same entity → update NewName
// Name:    "Rename Entity"
```

---

### 4.8 `ReparentEntityCommand`

```cpp
struct ReparentEntityData {
    ECS::EntityID Entity;
    ECS::EntityID OldParent;  // INVALID_ENTITY if was root
    ECS::EntityID NewParent;  // INVALID_ENTITY if becoming root
};
// Execute: ParentComponent on Entity → NewParent (or remove if INVALID)
// Undo:    ParentComponent on Entity → OldParent (or remove if INVALID)
// Name:    "Reparent Entity"
```

---

## 5. Compound commands

Some operations should undo as a single step even though they involve multiple mutations.

```cpp
// ZEngine/Editor/UndoRedo/CompoundCommand.h

class CompoundCommandBuilder {
public:
    void Begin(const char* name, Core::Memory::ArenaAllocator* arena);

    // Add a sub-command. Executes it immediately and records for undo.
    void Add(IEditorCommand cmd, const CommandContext& ctx);

    // Finalize into a single IEditorCommand.
    IEditorCommand End();

private:
    Core::Containers::Array<IEditorCommand> m_commands;
    const char* m_name = nullptr;
    Core::Memory::ArenaAllocator* m_arena = nullptr;
};

// Usage: "Paste 5 entities" should be one Ctrl+Z, not five
CompoundCommandBuilder builder;
builder.Begin("Paste", arena);
for (auto& entity : clipboard)
    builder.Add(MakeCreateEntityCommand(entity, arena), ctx);
undo_stack->Execute(builder.End(), ctx);
```

The compound command's Execute/Undo iterates sub-commands forward/backward.

---

## 6. Merge window

Continuous edits (gizmo drag, slider scrub) generate many commands per second.
Without merging, Ctrl+Z would undo one pixel of movement.

Merge policy (implemented in `UndoRedoStack::Execute` §3.1):
- Same `Execute` function pointer (same command type)
- Same entity (checked by comparing `Entity` field in Data)
- Within 200ms of the previous command of the same type
- `CommandMergeFn` returns true

The merged command retains the **original Old values** and updates the **New values**.
This means one Ctrl+Z always jumps back to the state before the entire drag started.

---

## 7. Keyboard shortcuts and Edit menu

```
Ctrl+Z         → UndoRedoStack::Undo()
Ctrl+Y         → UndoRedoStack::Redo()
Ctrl+Shift+Z   → UndoRedoStack::Redo()  (macOS convention)
```

Edit menu in Tetragrama's DockspaceUIComponent:

```
Edit
  Undo Move Entity     Ctrl+Z    ← greyed out if !CanUndo()
  Redo Delete Entity   Ctrl+Y    ← greyed out if !CanRedo()
  ──────────────────────────
  Duplicate            Ctrl+D
  Delete               Delete
```

`UndoName()` and `RedoName()` return the action name from the top command's `Name` fn.

---

## 8. Play mode integration

```cpp
// EditorPlayModeSystem::EnterPlay():
m_undo_stack->Clear();
// Reason: simulation is not undoable.
// Play takes its own snapshot via BinarySceneSerializer (editor-play-mode.md §5).
// The undo stack and the Play snapshot are separate mechanisms.
```

Undo is greyed out and non-functional in Play and Paused modes.

---

## 9. Edge cases

| Scenario | Behaviour |
|---|---|
| Stack full (100 commands) | Oldest command's `Destroy()` called, then slot recycled. No warning. |
| Undo after new action | Redo stack cleared; redo unavailable. |
| Undo Delete but entity was re-created with same name by game | New EntityID — undo recreates a second entity. Cannot avoid without UUID tracking. |
| Component size changed between undo/redo (DLL hot-reload) | Skip with `ZENGINE_CORE_WARN`: "Undo skipped: component layout changed since command was recorded." |
| Undo in Play/Paused mode | No-op. Ctrl+Z produces no effect. |
| Undo after Stop (stack was cleared) | No-op (stack is empty). |
| Field snapshot > 64 bytes (`ComponentFieldChangeCommand`) | Assert. For components with large fields (>64 bytes), use `DeleteEntityCommand` + `CreateEntityCommand` instead of field-level undo. |

---

## 10. File Layout

```
ZEngine/
  Editor/
    UndoRedo/
      EditorCommand.h               — IEditorCommand, CommandContext, function pointer typedefs
      UndoRedoStack.h/.cpp          — ring buffer, push/merge/undo/redo/clear
      CompoundCommandBuilder.h/.cpp — multi-command batch as single undo step
      Commands/
        TransformChangeCommand.h    — MakeTransformChangeCommand + data struct + fns
        ComponentFieldChangeCommand.h
        CreateEntityCommand.h
        DeleteEntityCommand.h
        AddComponentCommand.h
        RemoveComponentCommand.h
        RenameEntityCommand.h
        ReparentEntityCommand.h
```

All command headers are self-contained. No cross-includes between command types.

---

## 11. Deliverables Checklist

- [ ] `IEditorCommand` struct — Data, Execute, Undo, Destroy, Name, Merge, TimeNs
- [ ] `CommandContext` struct — Scene, Selection, Arena
- [ ] `UndoRedoStack` — ring buffer (100), Execute (with merge), Undo, Redo, Clear, CanUndo/Redo, UndoName/RedoName
- [ ] `CompoundCommandBuilder` — Begin, Add, End
- [ ] `TransformChangeCommand` — data struct, Execute, Undo, Merge, Name
- [ ] `ComponentFieldChangeCommand` — data struct, Execute, Undo, Merge, Name
- [ ] `CreateEntityCommand` — data struct, SnapshotEntity helper, Execute, Undo, Name
- [ ] `DeleteEntityCommand` — data struct, Execute, Undo, Name
- [ ] `AddComponentCommand` — data struct, Execute, Undo, Name
- [ ] `RemoveComponentCommand` — data struct (snapshot ≤512 bytes), Execute, Undo, Name
- [ ] `RenameEntityCommand` — data struct, Execute, Undo, Merge, Name
- [ ] `ReparentEntityCommand` — data struct, Execute, Undo, Name
- [ ] `SnapshotEntity` helper — ComponentReflectionRegistry-based binary dump
- [ ] `RestoreEntityFromSnapshot` helper — inverse of SnapshotEntity
- [ ] `UndoRedoStack::Clear()` called in `EditorPlayModeSystem::EnterPlay()`
- [ ] Ctrl+Z/Y/Shift+Z keyboard handling in DockspaceUIComponent
- [ ] Edit menu: Undo/Redo with dynamic names, greyed when unavailable
- [ ] InspectorViewUIComponent: capture old value before `IsItemDeactivatedAfterEdit`
- [ ] `tests/Editor/UndoRedoTest.cpp`:
  - [ ] Push + undo restores original value
  - [ ] Push + undo + redo re-applies command
  - [ ] Transform drag merges into one command (not 60 separate commands)
  - [ ] Undo delete restores entity with all components
  - [ ] Stack evicts oldest on overflow (capacity 100)
  - [ ] Clear empties both stacks; Ctrl+Z is no-op after clear
  - [ ] Compound command undoes as single step
  - [ ] Undo disabled in Play mode

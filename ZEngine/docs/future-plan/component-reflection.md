# ZEngine — Component Reflection

**Priority:** P2 — Core reflection and inspector are done; remaining items are plugin SDK and editor polish  
**Status:** Core complete; remaining work tracked in issues #704–#708  
**Depends on:** `actor-ecs-architecture.md` (`ComponentTypeID`, `ComponentTypeOf<T>`)  
**Relates to:** `scene-serialization.md` (`ComponentSerializerRegistry` — parallel registry, separate concern)  
**Blocks:** plugin component inspector (remaining), editor entity selection

---

## 1. What Reflection Is Here

Component reflection is **field-level metadata** for ECS component types. It answers:

- "What components does this entity have?" — for the inspector's component list
- "What fields does this component have?" — for displaying and editing them
- "What is the type, name, and byte offset of each field?" — for generic read/write

This is intentionally **minimal**. It is not full C++ reflection. There are no function
pointers, no inheritance graph, no method introspection. Just enough for the editor
inspector to display and edit any component — including components registered by
third-party plugins at runtime — without knowing their types at compile time.

---

## 2. Relationship to Existing Registries

ZEngine has one implemented component registry (`ComponentTypeOf<T>()`). Serialization and
reflection are two additional registries, both unimplemented today (design only):

```
ComponentTypeOf<T>()            → stable numeric ComponentTypeID per type  (source of truth)  IMPLEMENTED
ComponentSerializerRegistry     → serialize/deserialize functions per type (for save/load)     NOT YET BUILT
ComponentReflectionRegistry     → field metadata per type (for editor display/edit)            IMPLEMENTED
```

All three are indexed by `ComponentTypeID`. Registration is independent — a component
can be serializable without being reflectable (e.g., internal engine components that
should not appear in the inspector), and vice versa.

**Convergence rule**: `ComponentTypeOf<T>()` is the single source of truth for type
identity. Both `ComponentSerializerRegistry` and `ComponentReflectionRegistry` key on the
`ComponentTypeID` it returns. They must never define their own parallel ID schemes.

In practice, a component's `.cpp` file performs both registrations in adjacent static
initializers:

```cpp
// TransformComponent.cpp

// 1. Serialization (save/load)
static bool s_serializer = [] {
    ComponentSerializerRegistry::Get().Register(
        ComponentTypeOf<TransformComponent>(), { ... });
    return true;
}();

// 2. Reflection (inspector)
static bool s_reflection = [] {
    ComponentReflectionRegistry::Get().Register(
        ComponentTypeOf<TransformComponent>(), { ... });
    return true;
}();
```

This co-location ensures both registries are always in sync: adding a new field to a
component means updating one file, not hunting across two separate registration sites.
The two registries remain architecturally separate (neither includes the other's header)
but are populated from the same location.

---

## 3. `FieldType` Enum

The set of field types the inspector knows how to display and edit.

```cpp
// ZEngine/ECS/Reflection/FieldType.h
#pragma once
#include <cstdint>

namespace ZEngine::ECS {

    enum class FieldType : uint8_t {
        // Primitives
        Bool,
        Int8, Int16, Int32, Int64,
        UInt8, UInt16, UInt32, UInt64,
        Float, Double,

        // Math types (from Core/Maths/)
        Vec2f, Vec3f, Vec4f,
        Quatf,       // Quaternion<float>
        Mat4f,

        // Engine handle types
        EntityID,    // displayed as "Entity #N (gen M)" with a picker
        AssetUUID,   // displayed as asset name via AssetRegistry lookup

        // String — editor-editable, stored as fixed char[N] in the component
        String,

        // Enum — displayed as a dropdown; requires EnumMeta
        Enum,

        // Nested struct — a sub-group of fields; requires a nested FieldDescriptor array
        Struct,
    };

}  // namespace ZEngine::ECS
```

---

## 4. `FieldDescriptor`

Describes one field within a component struct.

```cpp
// ZEngine/ECS/Reflection/ComponentMeta.h
#pragma once
#include <ECS/Reflection/FieldType.h>
#include <Core/Containers/Array.h>
#include <cstdint>

namespace ZEngine::ECS {

    struct EnumValue {
        const char* Name  = nullptr;
        int64_t     Value = 0;
    };

    struct FieldDescriptor {
        const char* Name        = nullptr;   // "Speed", "IsAlive", "TargetPosition"
        FieldType   Type        = FieldType::Float;
        uint32_t    Offset      = 0;         // offsetof(ComponentT, field)
        uint32_t    Size        = 0;         // sizeof(field) — for bounds checks

        // FieldType::Float / Double — display range hint for slider
        float       Min         = -1e9f;
        float       Max         =  1e9f;

        // FieldType::String — capacity of the char[] buffer
        uint32_t    StringCap   = 0;

        // FieldType::Enum — pointer to null-terminated EnumValue array
        const EnumValue* EnumValues  = nullptr;
        uint32_t         EnumCount   = 0;

        // FieldType::Struct — nested fields
        const FieldDescriptor* SubFields  = nullptr;
        uint32_t               SubCount   = 0;

        // Editor visibility
        bool        Hidden      = false;    // do not show in inspector
        bool        ReadOnly    = false;    // show but do not allow editing
        const char* Tooltip     = nullptr;  // optional hover text
    };

}  // namespace ZEngine::ECS
```

---

## 5. `ComponentMeta`

Top-level metadata for one component type.

```cpp
// ZEngine/ECS/Reflection/ComponentMeta.h (continued)

    struct ComponentMeta {
        ComponentTypeID       TypeID    = 0;
        const char*           TypeName  = nullptr;  // "TransformComponent"
        uint32_t              Size      = 0;        // sizeof(ComponentT)
        uint32_t              Align     = 0;        // alignof(ComponentT)

        const FieldDescriptor* Fields   = nullptr;
        uint32_t               FieldCount = 0;

        // Category shown in the inspector's "Add Component" menu
        const char*           Category  = "General";  // "Transform", "Physics", "Audio", ...
        const char*           Tooltip   = nullptr;
    };
```

---

## 6. `ComponentReflectionRegistry`

Central store for all registered `ComponentMeta`. One instance per process.

```cpp
// ZEngine/ECS/Reflection/ComponentReflectionRegistry.h
#pragma once
#include <ECS/Reflection/ComponentMeta.h>
#include <ECS/ComponentTypeID.h>
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>

namespace ZEngine::ECS {

    class ComponentReflectionRegistry {
    public:
        static ComponentReflectionRegistry& Get();

        // Register metadata for a component type.
        // meta.Fields must point to a static or arena-allocated array that outlives
        // this registry. Do NOT pass stack-allocated FieldDescriptors.
        void Register(const ComponentMeta& meta);

        // Look up metadata by TypeID. Returns nullptr if not registered.
        [[nodiscard]] const ComponentMeta* Lookup(ComponentTypeID id) const;

        // Look up by stable type name string (for serialization and debugging).
        [[nodiscard]] const ComponentMeta* LookupByName(const char* type_name) const;

        // Iterate all registered component types in registration order.
        // Fn: void(const ComponentMeta&)
        template<typename Fn>
        void ForEach(Fn&& fn) const {
            for (uint32_t i = 0; i < m_metas.Size(); ++i)
                fn(m_metas[i]);
        }

        [[nodiscard]] uint32_t Count() const { return m_metas.Size(); }

        void Initialize(Core::Memory::ArenaAllocator* arena);

    private:
        Core::Containers::Array<ComponentMeta> m_metas;
        Core::Memory::ArenaAllocator*          m_arena = nullptr;
    };

}  // namespace ZEngine::ECS
```

---

## 7. Registration — How Engine Components Register Themselves

Each built-in component `.cpp` file defines a `RegisterXxxComponentReflection()` function
with static-storage field arrays. `BuiltInComponentReflection.cpp` provides a single
`RegisterBuiltInComponentReflection()` dispatcher that calls all eight functions; the
engine calls this once at startup after `ComponentReflectionRegistry::Initialize`.

Fields are declared as static arrays so their lifetimes outlive the registry.

```cpp
// ZEngine/ECS/Components/TransformComponent.cpp

#include <ECS/Reflection/ComponentReflectionRegistry.h>
#include <ECS/Components/TransformComponent.h>

static const ZEngine::ECS::FieldDescriptor kTransformFields[] = {
    {
        .Name    = "Position",
        .Type    = ZEngine::ECS::FieldType::Vec3f,
        .Offset  = offsetof(ZEngine::ECS::Components::TransformComponent, Position),
        .Size    = sizeof(ZEngine::Core::Maths::Vec3f),
        .Tooltip = "World-space position in metres",
    },
    {
        .Name   = "Rotation",
        .Type   = ZEngine::ECS::FieldType::Vec3f,
        .Offset = offsetof(ZEngine::ECS::Components::TransformComponent, Rotation),
        .Size   = sizeof(ZEngine::Core::Maths::Vec3f),
        .Tooltip = "Euler angles in radians (X=pitch, Y=yaw, Z=roll)",
    },
    {
        .Name   = "Scale",
        .Type   = ZEngine::ECS::FieldType::Vec3f,
        .Offset = offsetof(ZEngine::ECS::Components::TransformComponent, Scale),
        .Size   = sizeof(ZEngine::Core::Maths::Vec3f),
        .Min    = 0.001f, .Max = 1000.f,
    },
};

// Called once at engine startup, before WorldTick::Commit()
static bool s_registered = [] {
    ZEngine::ECS::ComponentReflectionRegistry::Get().Register({
        .TypeID     = ZEngine::ECS::ComponentTypeOf<TransformComponent>(),
        .TypeName   = "TransformComponent",
        .Size       = sizeof(TransformComponent),
        .Align      = alignof(TransformComponent),
        .Fields     = kTransformFields,
        .FieldCount = 3,
        .Category   = "Transform",
    });
    return true;
}();
```

The `static bool` pattern triggers registration at static initialization time — same
pattern as `ComponentSerializerRegistry`. Registration is idempotent if called twice
(second call with the same TypeID is a no-op with a debug warning).

---

## 8. How the Tetragrama Inspector Uses It

`InspectorPanel` (ZUI) is fully dynamic — no component type is named anywhere in the
panel code. All rendering and editing is driven by `ComponentReflectionRegistry::ForEach`
and `FieldDescriptor`. Plugin components are visible automatically.

**Component rendering loop** (`InspectorPanel::BuildContent`):

1. Get the entity's `ArchetypeMask` from `Scene::GetMask(id)`.
2. Call `ComponentReflectionRegistry::Get().ForEach(...)` — for each `ComponentMeta`
   whose `TypeID` is present in the mask, call `Scene::GetComponentRaw(id, meta.TypeID)`
   and render a collapsible section labelled `meta.TypeName`.
3. Inside each section, iterate `meta.Fields`. Skip `Hidden` fields. For `ReadOnly`
   fields render a non-editable display widget. Dispatch on `FieldDescriptor::Type`
   to the appropriate ZUI widget (drag-float, checkbox, text input, enum dropdown, etc.).
   Show `field.Tooltip` on hover when non-null.

**"Add Component" popup** (not yet implemented — see #704):

Below the component list, a button opens a popup that calls `ForEach` a second time
and lists only types not already on the entity. Selecting one calls
`Scene::AddComponentRaw(id, meta.TypeID, meta.Size, meta.Align)`.

**Field type dispatch** covers all `FieldType` values. Any variant not yet supported
by ZUI renders a read-only text label with the field name and a `(unsupported type)`
suffix rather than silently skipping it.

---

## 9. Plugin Integration

Plugins register component metadata via the Plugin SDK. The editor sees plugin
components exactly like engine components — no special handling needed.

```cpp
// In NavMeshPlugin.cpp — RegisterEditorPanels callback
#ifdef ZENGINE_EDITOR

static const ZFieldDescriptor kNavAgentFields[] = {
    { .Name="Speed",        .Type=ZFIELD_FLOAT, .Offset=offsetof(NavAgentComponent,Speed),        .Min=0.f,.Max=50.f },
    { .Name="StoppingDist", .Type=ZFIELD_FLOAT, .Offset=offsetof(NavAgentComponent,StoppingDist), .Min=0.f,.Max=10.f },
    { .Name="HasTarget",    .Type=ZFIELD_BOOL,  .Offset=offsetof(NavAgentComponent,HasTarget) },
    { .Name="ReachedTarget",.Type=ZFIELD_BOOL,  .Offset=offsetof(NavAgentComponent,ReachedTarget),.ReadOnly=true },
    { .Name="PathPoints",   .Type=ZFIELD_UINT32,.Offset=offsetof(NavAgentComponent,PathPointCount),.ReadOnly=true,
      .Tooltip="Current number of waypoints remaining" },
};

void NavPlugin_RegisterEditorPanels(const ZPluginContext* ctx) {
    ZPlugin_RegisterComponentMeta(ctx->Editor, &(ZComponentMetaDesc){
        .TypeID     = g_state->NavAgentTypeID,
        .TypeName   = "NavAgentComponent",
        .Fields     = kNavAgentFields,
        .FieldCount = 5,
        .Category   = "AI",
    });
    // ... other editor registrations
}
#endif
```

---

## 10. New ECS::Scene methods required

The inspector needs two new raw-pointer methods that don't exist yet:

```cpp
// Add to ECS::Scene:

// Returns a raw void* to the component data. Caller casts using ComponentMeta::Size/Align.
// Returns nullptr if entity doesn't have the component.
[[nodiscard]] void* GetComponentRaw(EntityID id, ComponentTypeID type_id);

// Adds a zero-initialized component of the given type and size.
// Used by the "Add Component" button in the inspector.
// Equivalent to AddComponent<T> but type-erased.
void AddComponentRaw(EntityID id, ComponentTypeID type_id,
                     uint32_t size, uint32_t align);
```

These route through `IComponentStorage::GetRaw` and `IComponentStorage::AddRaw` on the
appropriate storage in `Scene::m_storages`. The type-safe template methods remain the
primary API; these raw methods are for editor use only.

---

## 11. `ZPlugin_RegisterComponentMeta` in the Plugin SDK

Add to `PluginEditor.h`:

```cpp
// ZEngine/PluginSDK/PluginEditor.h

struct ZComponentMetaDesc {
    uint32_t              TypeID;      // from ZPlugin_RegisterComponentType
    const char*           TypeName;
    const ZFieldDescriptor* Fields;
    uint32_t              FieldCount;
    const char*           Category;   // inspector group (default: "General")
    const char*           Tooltip;    // optional
};

struct ZFieldDescriptor {
    const char* Name;
    uint8_t     Type;       // maps to FieldType enum values (stable integers)
    uint32_t    Offset;
    uint32_t    Size;
    float       Min, Max;   // for float/int sliders
    uint32_t    StringCap;  // for string fields
    bool        ReadOnly;
    bool        Hidden;
    const char* Tooltip;
    // Enum and nested struct support deferred to SDK v1.1
};

// Register component field metadata for the editor inspector.
// Only meaningful in editor builds (ZEditorHandle != null).
// No-op in shipping builds.
void ZPlugin_RegisterComponentMeta(ZEditorHandle editor,
                                   const ZComponentMetaDesc* desc);
```

`ZFieldDescriptor` uses plain integers for `Type` rather than the `FieldType` enum
to preserve C ABI stability — the enum values are published as stable constants in
`PluginTypes.h`.

---

## 12. File Layout

```
ZEngine/
  ECS/
    Reflection/
      FieldType.h                      — FieldType enum + stable integer constants
      ComponentMeta.h                  — FieldDescriptor, EnumValue, ComponentMeta structs
      ComponentReflectionRegistry.h/.cpp — register / lookup / ForEach
      BuiltInComponentReflection.h/.cpp  — RegisterBuiltInComponentReflection() dispatcher

  ECS/Components/
    TransformComponent.cpp             — RegisterTransformComponentReflection()
    MeshComponent.cpp                  — RegisterMeshComponentReflection()
    CameraComponent.cpp                — RegisterCameraComponentReflection()
    LightComponent.cpp                 — RegisterLightComponentReflection()
    MaterialComponent.cpp              — RegisterMaterialComponentReflection()
    NameComponent.cpp                  — RegisterNameComponentReflection()
    RigidBodyComponent.cpp             — RegisterRigidBodyComponentReflection()
    UUIDComponent.cpp                  — RegisterUUIDComponentReflection()

  PluginSDK/
    PluginEditor.h                     — ZPlugin_RegisterComponentMeta + ZFieldDescriptor (not yet built — #705)
```

---

## 13. What this replaced in Tetragrama

The old `InspectorViewUIComponent` (ImGui) used per-component hardcoded blocks via the
`Actor*` tier-1 API. That class has been removed. `InspectorPanel` (ZUI) replaced it and
is fully dynamic — all rendering is driven by `ComponentReflectionRegistry::ForEach` with
no per-component code. The migration is complete.

The panel receives an `Actor*` from `ActorManager` and extracts `actor->GetEntityID()` to
call `Scene::GetComponentRaw(EntityID, TypeID)`. Any component — engine, game, or plugin
— is displayed automatically as long as it has registered metadata.

---

## 14. Deliverables Checklist

- [x] `ZEngine/ECS/Reflection/FieldType.h` — enum + stable integer constants
- [x] `ZEngine/ECS/Reflection/ComponentMeta.h` — `FieldDescriptor`, `EnumValue`, `ComponentMeta`
- [x] `ZEngine/ECS/Reflection/ComponentReflectionRegistry.h/.cpp` — `Register`, `Lookup`, `LookupByName`, `ForEach`
- [x] `ECS::Scene::GetComponentRaw(EntityID, ComponentTypeID) → void*`
- [ ] `ECS::Scene::AddComponentRaw(EntityID, ComponentTypeID, size, align)` — #706
- [x] All built-in component `.cpp` files register reflection metadata for the 8 actual components:
  `TransformComponent`, `MeshComponent`, `CameraComponent`, `LightComponent`,
  `MaterialComponent`, `NameComponent`, `RigidBodyComponent`, `UUIDComponent`
- [ ] `PluginEditor.h`: `ZFieldDescriptor`, `ZComponentMetaDesc`, `ZPlugin_RegisterComponentMeta` — #705
- [x] `InspectorPanel` (ZUI) fully dynamic — no per-component hardcoded blocks; migration from ImGui complete
- [ ] "Add Component" button in inspector using `ComponentReflectionRegistry::ForEach` — #704
- `tests/ECS/ComponentReflectionTest.cpp`:
  - [x] All 8 built-ins registered (`AllEightBuiltInsAreRegistered`)
  - [x] Lookup by TypeID and by name return same pointer (`LookupByTypeIDMatchesLookupByName`)
  - [x] Unregistered returns nullptr, nullptr name returns nullptr (`LookupMissesReturnNull`)
  - [x] `ForEach` visits all 8 in registration order (`ForEachVisitsAllEightInRegistrationOrder`)
  - [x] String field has correct cap and flags (`NameComponentValueIsEditableStringWithCap128`)
  - [x] ReadOnly and Hidden flags match spec for TransformComponent, MeshComponent, RigidBodyComponent (`HiddenAndReadOnlyFlagsMatchSpec`)
  - [x] UUID field is ReadOnly (`UUIDComponentValueIsReadOnly`)
  - [x] Enum fields carry value tables for LightComponent and RigidBodyComponent (`EnumFieldsCarryTheirValueTables`)
  - [x] Every field's `offset + size <= component size` (`EveryFieldFitsWithinItsComponent`)
  - [ ] Exact `offsetof` assertions — verify `field.Offset == offsetof(ComponentT, fieldName)` — #707
  - [ ] Plugin registers metadata, inspector displays it generically — #708

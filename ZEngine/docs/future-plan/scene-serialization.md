# Scene Serialization — YAML (Dev) + Binary (Ship)

**Priority:** P3 — Implement after ECS core, VFS Tickets 5–6, and import pipeline  
**Status:** Design — none of this architecture is built yet  
**Depends on:** `actor-ecs-architecture.md`, VFS Tickets 5–6 (stable UUIDs + AssetRegistry — unblocked on VFS side), `import-pipeline.md`  
**Unblocked by:** `ZENGINE_EDITOR` compile definition now available; `uuids::uuid` (stduuid) vendored  
**Blocks:** Structured editor scene save/load, scene cook pipeline

---

## Current State (interim implementation)

`Tetragrama/Serializers/EditorSceneSerializer` is the **working** scene serializer today.
It is NOT this design — it is a temporary implementation that will be replaced.

| Property | Current (`EditorSceneSerializer`) | Target (this doc) |
|---|---|---|
| Format | Custom binary `.zescene` (ZESCENE_MAGIC + SCENE_FILE_VERSION) | YAML dev / Binary ship |
| Scope | Serializes `EditorScene` (Tetragrama type) directly | Serializes ECS `Scene` via component registry |
| Component serialization | Hardcoded per-component blocks | `ComponentSerializerRegistry` — dynamic, plugin-safe |
| VFS integration | Native `std::fstream` with resolved native paths | `IVFSContext` — VFS-native, testable in memory |
| Tests | None | 8 tests planned in `tests/Scene/SceneSerializationTest.cpp` |
| Location | `Tetragrama/Serializers/EditorSceneSerializer.h/.cpp` | `ZEngine/Scene/` (engine-layer) |

The `EditorSceneSerializer` is not to be confused with the designed system. Do not extend it —
feature work goes into the new `ISceneSerializer` architecture described below.

---

**Goal**: Serialize and deserialize `Scene` objects through a single `ISceneSerializer`
interface backed by two implementations: `YAMLSceneSerializer` for development (human-readable,
VCS-diffable) and `BinarySceneSerializer` for shipping (zero-parse, memcpy-speed load).
Asset references are always `uuids::uuid` — never file paths.

---

## 1. Scene Struct

```cpp
// ZEngine/Scene/Scene.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/Strings.h>
#include <ECS/EntityID.h>
#include <uuid.h>

namespace ZEngine::Scene
{
    struct Scene
    {
        uuids::uuid                        SceneUUID = {};
        Core::Containers::String           Name      = {};
        Core::Containers::Array<ECS::EntityID> Entities = {};
    };
}
```

`Scene` owns no component data — the ECS `EntityRegistry` owns that. The scene is
a named collection of entity IDs with a stable UUID for serialized identity.

---

## 2. `ISceneSerializer`

```cpp
// ZEngine/Scene/ISceneSerializer.h
#pragma once
#include <Scene/Scene.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>
#include <VFS/VFSResult.h>

namespace ZEngine::Scene
{
    class ISceneSerializer
    {
    public:
        virtual ~ISceneSerializer() = default;

        virtual VFS::VFSResult<void> Serialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            const Scene& scene) = 0;

        virtual VFS::VFSResult<void> Deserialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            Scene& out_scene) = 0;
    };
}
```

---

## 3. Component Serialization Registry

Components register their own serialize/deserialize functions so the serializer
never has a hardcoded list of component types.

```cpp
// ZEngine/Scene/ComponentSerializerRegistry.h
#pragma once
#include <ECS/ComponentTypeID.h>
#include <ECS/EntityID.h>
#include <functional>
#include <nlohmann/json.hpp>

namespace ZEngine::Scene
{
    struct ComponentSerializeFns
    {
        // YAML path: write component data into a json object
        std::function<void(ECS::EntityID, nlohmann::json&)>       SerializeYAML;
        // YAML path: read component data from a json object, apply to entity
        std::function<void(ECS::EntityID, const nlohmann::json&)> DeserializeYAML;

        // Binary path: write component data into a byte buffer
        std::function<void(ECS::EntityID, Core::Containers::Array<uint8_t>&)> SerializeBinary;
        // Binary path: read component data from a byte span, apply to entity
        std::function<void(ECS::EntityID, const uint8_t* data, uint32_t size)> DeserializeBinary;
    };

    class ComponentSerializerRegistry
    {
    public:
        static ComponentSerializerRegistry& Get();

        void Register(ECS::ComponentTypeID type_id, ComponentSerializeFns fns);

        const ComponentSerializeFns* Lookup(ECS::ComponentTypeID type_id) const;

        // Iterates all registered type IDs in stable order
        void ForEach(std::function<void(ECS::ComponentTypeID, const ComponentSerializeFns&)> fn) const;

    private:
        Core::Containers::Array<ECS::ComponentTypeID>       m_ids;
        Core::Containers::Array<ComponentSerializeFns>      m_fns;
    };
}
```

**Implementation note**: The `std::function` fields shown above are pseudocode for clarity. The actual implementation must use the engine's C-style callback convention — `{ void* Context; void (*Fn)(...); }` — consistent with `ImportCompleteCallback`, `MainThreadScheduler::Post`, and other engine APIs. `std::function` allocates on capture and is not allowed in engine-layer types.

**Registration example** (in a component's `.cpp`):
```cpp
// TransformComponent.cpp
static bool s_registered = [] {
    ComponentSerializerRegistry::Get().Register(
        ComponentTypeID::Of<TransformComponent>(),
        {
            .SerializeYAML = [](EntityID id, nlohmann::json& j) {
                auto& t = ECS::Get<TransformComponent>(id);
                j["position"] = {t.Position.x, t.Position.y, t.Position.z};
                j["rotation"] = {t.Rotation.x, t.Rotation.y, t.Rotation.z, t.Rotation.w};
                j["scale"]    = {t.Scale.x,    t.Scale.y,    t.Scale.z};
            },
            .DeserializeYAML = [](EntityID id, const nlohmann::json& j) {
                auto& t = ECS::GetOrAdd<TransformComponent>(id);
                auto p = j["position"]; t.Position = {p[0], p[1], p[2]};
                auto r = j["rotation"]; t.Rotation = {r[0], r[1], r[2], r[3]};
                auto s = j["scale"];    t.Scale    = {s[0], s[1], s[2]};
            },
            // ... binary fns ...
        });
    return true;
}();
```

---

## 4. YAML Schema

File extension: `.scene.yaml`. Example:

```yaml
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: "MainLevel"
  entities:
    - id: 1
      name: "PlayerMesh"
      components:
        transform:
          position: [0.0, 1.5, 0.0]
          rotation: [0.0, 0.0, 0.0, 1.0]
          scale:    [1.0, 1.0, 1.0]
        mesh:
          asset_uuid: "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
        material:
          asset_uuid: "deadbeef-dead-beef-dead-beefdeadbeef"

    - id: 2
      name: "SunLight"
      components:
        transform:
          position: [100.0, 200.0, 50.0]
          rotation: [0.0, 0.0, 0.0, 1.0]
          scale:    [1.0, 1.0, 1.0]
        directional_light:
          color:     [1.0, 0.95, 0.85]
          intensity: 3.0
```

**Invariants enforced on read**:
- Every asset reference (`asset_uuid`) must be a valid UUID string. File paths are rejected.
- Unknown keys are silently ignored (forward compatibility).
- `id` values must be unique within the scene.

---

## 5. `YAMLSceneSerializer`

Available only in editor builds (`#ifdef ZENGINE_EDITOR`).

```cpp
// ZEngine/Scene/YAMLSceneSerializer.h
#pragma once
#ifdef ZENGINE_EDITOR
#include <Scene/ISceneSerializer.h>

namespace ZEngine::Scene
{
    class YAMLSceneSerializer final : public ISceneSerializer
    {
    public:
        VFS::VFSResult<void> Serialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            const Scene& scene) override;

        VFS::VFSResult<void> Deserialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            Scene& out_scene) override;

    private:
        // Returns Fail if any asset reference is a file path instead of a UUID
        static VFS::VFSResult<void> ValidateAssetRefs(const nlohmann::json& entity_json);
    };
}
#endif // ZENGINE_EDITOR
```

**`Deserialize` implementation notes**:
- Use `nlohmann::json::parse(..., nullptr, /*allow_exceptions=*/false)`; check `is_discarded()`
- Call `ValidateAssetRefs` on each entity block before constructing any `EntityID`
- Route each component key to `ComponentSerializerRegistry::Lookup(type_id)->DeserializeYAML`
- Unknown component keys: emit a warning log, skip — do not fail

**`ValidateAssetRefs`**: walks the json tree; for any key ending in `_uuid`, checks the value
matches UUID regex `[0-9a-f]{8}-[0-9a-f]{4}-...`; returns `VFSError::InvalidData` if a
path-like string (`/`, `\`, `.glb`, `.png`) is found instead.

---

## 6. Binary Format

File extension: `.scene.bin`.

```
[SceneHeader]               16 bytes
  magic:                uint32   = 0x5A534345  ('ZSCE')
  version:              uint16
  flags:                uint16
  entity_count:         uint32
  component_table_offset: uint64  ← byte offset into file

[EntityBlock × entity_count]     20 bytes each
  id:                   uint64
  name_offset:          uint32   ← byte offset into StringPool
  component_mask:       uint64   ← bitfield, one bit per ComponentTypeID

[ComponentSection × registered_types]
  [ComponentSectionHeader]       12 bytes
    type_id:            uint32
    entry_count:        uint32
    data_size:          uint32
  [EntityID × entry_count]       8 bytes each
  [ComponentData × entry_count]  variable, tightly packed

[StringPool]
  null-terminated UTF-8 strings, concatenated
```

**Load path**: `mmap` (or `ReadAt` into arena), fix up offsets, done. No heap allocation.
`StringPool` pointers are `const char*` into the mapped region — valid for the lifetime
of the scene file handle.

---

## 7. `BinarySceneSerializer`

```cpp
// ZEngine/Scene/BinarySceneSerializer.h
#pragma once
#include <Scene/ISceneSerializer.h>

namespace ZEngine::Scene
{
    constexpr uint32_t SCENE_BINARY_MAGIC   = 0x5A534345u;
    constexpr uint16_t SCENE_BINARY_VERSION = 1;

    class BinarySceneSerializer final : public ISceneSerializer
    {
    public:
        VFS::VFSResult<void> Serialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            const Scene& scene) override;

        VFS::VFSResult<void> Deserialize(
            VFS::IVFSContext& ctx,
            const VFS::VFSPath& path,
            Scene& out_scene) override;
    };
}
```

**`Serialize` implementation notes**:
- Build `StringPool` first (collect all entity names, dedup, record offsets)
- Write `SceneHeader`, `EntityBlock[]`, `ComponentSection[]` in order, then `StringPool`
- Use `ComponentSerializerRegistry::ForEach` to iterate in stable registered order
- All writes go through `IVFSContext::OpenFile(path, Write | Create | Truncate)`

**`Deserialize` implementation notes**:
- Read full file into arena buffer via `ReadAt`
- Validate `magic` and `version`; reject unknown versions
- Reconstruct `EntityID` list from `EntityBlock[]`
- For each `ComponentSection`, look up `ComponentSerializerRegistry::Lookup(type_id)->DeserializeBinary`
- Unknown `type_id` in file: skip the section (forward compatibility)

---

## 8. Versioning and Migration

```cpp
// ZEngine/Scene/SceneMigration.h
#pragma once
#include <VFS/VFSResult.h>

namespace ZEngine::Scene
{
    // Called by BinarySceneSerializer::Deserialize when version < SCENE_BINARY_VERSION
    VFS::VFSResult<void> MigrateScene(uint8_t* data, uint32_t size,
                                      uint16_t from_version, uint16_t to_version);
}
```

Migration table maps `(from, to)` → upgrade function. Each upgrade function is a pure
byte transformation on the arena buffer. YAML is forward-compatible by design (unknown
keys ignored); no migration needed for the YAML format.

---

## 9. Asset Reference Validation on Load

```cpp
// In YAMLSceneSerializer::Deserialize, after parsing each asset_uuid field:
auto parsed = uuids::uuid::from_string(uuid_str);
if (!parsed.has_value())
    return VFSResult<void>::Fail(VFSError::InvalidData);  // was a path, not a UUID

// Optional: warn if UUID not found in AssetRegistry (don't crash — asset may still be loading)
if (!AssetRegistry::Get().LookupByUUID(parsed.value()))
    ZENGINE_LOG_WARN("Scene references unknown asset UUID: {}", uuid_str);
```

The engine must never store a file path in a scene node. This is enforced at the
serializer boundary — not a convention, not a lint rule.

## Asset Reference Policy

```cpp
// After parsing each asset_uuid string from the scene file:
auto parsed = uuids::uuid::from_string(uuid_str);
if (!parsed.has_value()) {
    return VFSResult<void>::Fail(VFSError::InvalidData);  // not a valid UUID string
}

const uuids::uuid& uuid = parsed.value();

// In EDITOR builds: warn and substitute placeholder asset if UUID not found.
// In SHIPPING builds: fail hard — a scene with missing assets cannot be used.
#ifdef ZENGINE_EDITOR
    if (!AssetRegistry::Get().Exists(uuid)) {
        ZENGINE_CORE_WARN("Scene references missing asset UUID: {}. "
                          "Using placeholder asset.", uuid_str);
        // use placeholder mesh / texture handle
    }
#else
    ZENGINE_VALIDATE_ASSERT(AssetRegistry::Get().Exists(uuid),
        "Scene serialization: asset UUID not found in registry: %s. "
        "Rebuild the scene or reimport missing assets.", uuid_str);
#endif
```

**Policy summary:**
- In editor builds, a missing asset UUID emits a warning and substitutes a placeholder, allowing the scene to load and the artist to fix the reference without a hard crash.
- In shipping builds, a missing asset UUID is a fatal error. Scenes must be validated during the cook step (Section 10) to ensure all referenced UUIDs are present before a build ships. A scene with missing assets in a shipping build indicates a broken cook pipeline and must not silently produce a degraded experience.

---

## 10. Actor Serialization Rule

`Actor` subclasses may hold private C++ fields (e.g., `PlayerActor::m_JumpCooldown`).
These are **not** serialized by the scene serializer. The rule is:

> Any state that must survive a save/load cycle must live in a component, not on the Actor object.

This is intentional. It keeps the serializer component-driven and avoids a second
serialization path. The enforcement strategy:

- `Actor` base class has no `Serialize` virtual method and will not get one.
- Engine components (`TransformComponent`, `RigidBodyComponent`, etc.) register with
  `ComponentSerializerRegistry` and are serialized automatically.
- Gameplay-specific state that needs persistence belongs in a custom component
  (e.g., `PlayerStateComponent { float JumpCooldown; int Health; }`), registered
  and owned by the Actor.

**What this means for `Actor::OnCreate`**: initialize from component state, not from
Actor member variables. If the Actor is loaded from a scene file, `OnCreate` will be
called after components are deserialized — read the component values, not default
member initializers.

```cpp
void PlayerActor::OnCreate() override {
    AddComponent<TransformComponent>({});
    AddComponent<PlayerStateComponent>({ .Health = 100 });
    // m_JumpCooldown is NOT persisted — derive it from PlayerStateComponent on load
}
```

---

## 11. Cook Integration

```
Cook step for a scene:
  1. YAMLSceneSerializer::Deserialize(".scene.yaml") → Scene
  2. Validate all UUIDs present in AssetRegistry (error if any Missing)
  3. BinarySceneSerializer::Serialize(".scene.bin") → write to pak staging dir
  4. CookManifest records SHA256(source .yaml) → skip on incremental cook if unchanged
```

The runtime only links `BinarySceneSerializer`. `YAMLSceneSerializer` is excluded from
shipping builds via `#ifdef ZENGINE_EDITOR`.

---

## 12. Unit Tests

File: `ZEngine/tests/Scene/SceneSerializationTest.cpp`

### Test 1 — YAML round-trip preserves all entity IDs
```cpp
TEST(YAMLSceneSerializer, RoundTripEntityIDs)
{
    MemoryVFSContext ctx;
    YAMLSceneSerializer s;
    Scene scene;
    scene.SceneUUID = uuids::uuid_random_generator{}();
    scene.Name = "Test";
    scene.Entities = {EntityID{1,0}, EntityID{2,0}, EntityID{3,0}};

    VFSPath path = VFSPath::Parse("/scene.yaml").Value();
    ASSERT_TRUE(s.Serialize(ctx, path, scene).IsOk());

    Scene loaded;
    ASSERT_TRUE(s.Deserialize(ctx, path, loaded).IsOk());
    EXPECT_EQ(loaded.Entities.Size(), 3u);
    EXPECT_EQ(loaded.SceneUUID, scene.SceneUUID);
}
```

### Test 2 — Binary round-trip preserves all entity IDs
```cpp
TEST(BinarySceneSerializer, RoundTripEntityIDs)
{
    MemoryVFSContext ctx;
    BinarySceneSerializer s;
    Scene scene;
    scene.SceneUUID = uuids::uuid_random_generator{}();
    scene.Name = "BinTest";
    scene.Entities = {EntityID{10,0}, EntityID{20,0}};

    VFSPath path = VFSPath::Parse("/scene.bin").Value();
    ASSERT_TRUE(s.Serialize(ctx, path, scene).IsOk());

    Scene loaded;
    ASSERT_TRUE(s.Deserialize(ctx, path, loaded).IsOk());
    EXPECT_EQ(loaded.Entities.Size(), 2u);
    EXPECT_EQ(loaded.SceneUUID, scene.SceneUUID);
}
```

### Test 3 — YAML rejects file path in asset reference
```cpp
TEST(YAMLSceneSerializer, RejectsFilePathAsAssetRef)
{
    MemoryVFSContext ctx;
    ctx.WriteFile("/bad.scene.yaml", R"(
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: "Bad"
  entities:
    - id: 1
      components:
        mesh:
          asset_uuid: "/assets/mesh.glb"
)");
    YAMLSceneSerializer s;
    Scene out;
    auto result = s.Deserialize(ctx, VFSPath::Parse("/bad.scene.yaml").Value(), out);
    EXPECT_FALSE(result.IsOk());
}
```

### Test 4 — YAML with unknown component key does not fail
```cpp
TEST(YAMLSceneSerializer, UnknownComponentKeyIgnored)
{
    MemoryVFSContext ctx;
    ctx.WriteFile("/unknown.scene.yaml", R"(
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440001"
  name: "UnknownComp"
  entities:
    - id: 1
      components:
        future_component_not_yet_registered:
          data: 42
)");
    YAMLSceneSerializer s;
    Scene out;
    EXPECT_TRUE(s.Deserialize(ctx, VFSPath::Parse("/unknown.scene.yaml").Value(), out).IsOk());
}
```

### Test 5 — Missing asset UUID emits warning, does not fail
```cpp
TEST(YAMLSceneSerializer, MissingAssetUUIDWarnsNotFails)
{
    MemoryVFSContext ctx;
    // AssetRegistry is empty — UUID is valid format but not registered
    ctx.WriteFile("/warn.scene.yaml", R"(
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440002"
  name: "WarnScene"
  entities:
    - id: 1
      components:
        mesh:
          asset_uuid: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
)");
    YAMLSceneSerializer s;
    Scene out;
    // Should succeed with a warning log, not an error
    EXPECT_TRUE(s.Deserialize(ctx, VFSPath::Parse("/warn.scene.yaml").Value(), out).IsOk());
}
```

### Test 6 — Component registry serialization round-trip
```cpp
TEST(SceneSerializer, ComponentRegistryRoundTrip)
{
    // Register a mock component with YAML fns
    struct ColorComponent { float R, G, B; };
    ComponentSerializerRegistry::Get().Register(
        ComponentTypeID::Of<ColorComponent>(),
        {
            .SerializeYAML = [](EntityID id, nlohmann::json& j) {
                auto& c = TestECS::Get<ColorComponent>(id);
                j["r"] = c.R; j["g"] = c.G; j["b"] = c.B;
            },
            .DeserializeYAML = [](EntityID id, const nlohmann::json& j) {
                auto& c = TestECS::GetOrAdd<ColorComponent>(id);
                c.R = j["r"]; c.G = j["g"]; c.B = j["b"];
            },
        });

    MemoryVFSContext ctx;
    YAMLSceneSerializer s;
    // ... serialize scene with ColorComponent, deserialize, verify R/G/B match
}
```

### Test 7 — Binary version mismatch returns Fail
```cpp
TEST(BinarySceneSerializer, VersionMismatchFails)
{
    MemoryVFSContext ctx;
    // Write a binary scene with a future version number
    uint8_t bad_header[16] = {};
    *reinterpret_cast<uint32_t*>(bad_header)     = SCENE_BINARY_MAGIC;
    *reinterpret_cast<uint16_t*>(bad_header + 4) = 0xFFFF;  // unknown version
    ctx.WriteRaw("/future.scene.bin", bad_header, sizeof(bad_header));

    BinarySceneSerializer s;
    Scene out;
    auto result = s.Deserialize(ctx, VFSPath::Parse("/future.scene.bin").Value(), out);
    EXPECT_FALSE(result.IsOk());
}
```

### Test 8 — Empty scene serializes and deserializes cleanly
```cpp
TEST(BinarySceneSerializer, EmptyScene)
{
    MemoryVFSContext ctx;
    BinarySceneSerializer s;
    Scene empty;
    empty.SceneUUID = uuids::uuid_random_generator{}();
    empty.Name = "Empty";

    VFSPath path = VFSPath::Parse("/empty.scene.bin").Value();
    ASSERT_TRUE(s.Serialize(ctx, path, empty).IsOk());

    Scene loaded;
    ASSERT_TRUE(s.Deserialize(ctx, path, loaded).IsOk());
    EXPECT_EQ(loaded.Entities.Size(), 0u);
    EXPECT_EQ(loaded.SceneUUID, empty.SceneUUID);
}
```

---

## 13. Deliverables Checklist

- [ ] `ZEngine/Scene/Scene.h`
- [ ] `ZEngine/Scene/ISceneSerializer.h`
- [ ] `ZEngine/Scene/ComponentSerializerRegistry.h` + `.cpp`
- [ ] `ZEngine/Scene/YAMLSceneSerializer.h` + `.cpp` (`#ifdef ZENGINE_EDITOR`)
- [ ] `ZEngine/Scene/BinarySceneSerializer.h` + `.cpp`
- [ ] `ZEngine/Scene/SceneMigration.h` + `.cpp`
- [ ] `TransformComponent`, `MeshComponent`, `MaterialComponent` register YAML + binary fns
- [ ] `ValidateAssetRefs` rejects any path-like string in `*_uuid` fields
- [ ] Cook step wired: `.scene.yaml` → `YAMLSceneSerializer` → `BinarySceneSerializer` → pak
- [ ] `#ifdef ZENGINE_EDITOR` excludes `YAMLSceneSerializer` from ship builds
- [ ] `tests/Scene/SceneSerializationTest.cpp` (8 tests)
- [ ] Manual smoke test: save scene in editor → open in hex editor → confirm `ZSCE` magic; reload in editor → entity names intact

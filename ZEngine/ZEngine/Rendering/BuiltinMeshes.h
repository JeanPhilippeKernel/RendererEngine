#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <uuid.h>

namespace ZEngine::Rendering
{
    // UUID derivation rule (collision-free by construction):
    //   All importer UUIDs are v4 random — third group always starts with '4'.
    //   Builtin UUIDs use third group '0000', which v4 can never produce.
    //
    //   Mesh:     ff000000-0000-0000-0000-000000000001  (last group = enum value + 1)
    //   Material: ff000000-0000-0001-0000-000000000001  (same counter, different variant field)

    enum class BuiltinMeshID : uint32_t
    {
        // Editor icons — tiny geometry, emissive material, component-type bound
        DirectionalLightIcon = 0,
        // PointLightIcon    = 1,   (future)
        // SpotLightIcon     = 2,   (future)
        // CameraIcon        = 3,   (future)
        // RigidBodyWire     = 4,   (future)

        // Primitive shapes — unit-scale, white/gray albedo, user-spawnable
        // Cube              = 5,   (future)
        // Sphere            = 6,   (future)
        // Cylinder          = 7,   (future)
        // Plane             = 8,   (future)
        // Capsule           = 9,   (future)

        COUNT
    };

    // Raw UUID string for the mesh — compile-time pointer, no allocation.
    const char* BuiltinMeshUUID(BuiltinMeshID id);

    // Parsed mesh UUID — for MeshComponent / AddMeshInstance.
    uuids::uuid BuiltinMeshUUIDParsed(BuiltinMeshID id);

    // Parsed paired material UUID. Returns nil UUID if the entry has no material.
    uuids::uuid BuiltinMaterialUUIDParsed(BuiltinMeshID id);

    // Iterates the table and calls AssetManager::IngestMaterial + IngestMesh for
    // every entry. Material is ingested first (must exist before the first render).
    // Both calls deduplicate by UUID — safe to call multiple times.
    void        RegisterBuiltinMeshes(Core::Memory::ArenaAllocator* arena);

} // namespace ZEngine::Rendering

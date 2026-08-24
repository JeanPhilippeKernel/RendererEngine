#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/AssetTypes.h>

namespace ZEngine::Rendering
{
    // Reserved prefix ff000000- will not collide with importer-assigned UUIDs.
    inline constexpr const char* DIRECTIONAL_LIGHT_MESH_UUID = "ff000000-0000-0000-0000-000000000001";

    // Sun-shaped directional light icon mesh:
    //   - 12-sided disc + 8 diamond rays in the local XY plane (the "source" face)
    //   - Arrow along local +Z (the light travel direction), built as two
    //     perpendicular flat planes for 360° visibility without a billboard shader
    //
    // Vertex layout: 8 floats (x y z  nx ny nz  u v).
    // All arrays are allocated from arena. Pass the results to AssetManager::IngestMesh.
    void                         CreateDirectionalLightMesh(Core::Memory::ArenaAllocator* arena, Importers::AssetMesh& out_mesh, Importers::AssetNodeHierarchy& out_hierarchy);
} // namespace ZEngine::Rendering

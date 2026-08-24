#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/BuiltinMeshes.h>
#include <uuid.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Importers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering
{
    using BuildMeshFn     = void (*)(ArenaAllocator*, AssetMesh&, AssetNodeHierarchy&);
    using BuildMaterialFn = void (*)(AssetMaterial&); // null = no paired material

    struct BuiltinMeshEntry
    {
        const char*     MeshUUID;
        const char*     MaterialUUID; // null if no material
        BuildMeshFn     BuildMesh;
        BuildMaterialFn BuildMaterial; // null if no material
    };

    static void BuildDirectionalLightIcon(ArenaAllocator* arena, AssetMesh& out_mesh, AssetNodeHierarchy& out_hierarchy)
    {
        out_mesh.Vertices.init(arena, 512);
        out_mesh.Indices.init(arena, 256);

        auto push_v = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v) {
            out_mesh.Vertices.push(x);
            out_mesh.Vertices.push(y);
            out_mesh.Vertices.push(z);
            out_mesh.Vertices.push(nx);
            out_mesh.Vertices.push(ny);
            out_mesh.Vertices.push(nz);
            out_mesh.Vertices.push(u);
            out_mesh.Vertices.push(v);
        };
        auto push_tri = [&](uint32_t a, uint32_t b, uint32_t c) {
            out_mesh.Indices.push(a);
            out_mesh.Indices.push(b);
            out_mesh.Indices.push(c);
        };
        auto            vcount   = [&]() -> uint32_t { return static_cast<uint32_t>(out_mesh.Vertices.size() / 8); };

        constexpr float PI       = 3.14159265358979323846f;
        constexpr float TWO_PI   = 2.f * PI;
        constexpr int   DISC_SEG = 12;

        // Disc (12-gon in XY plane, normal +Z)
        uint32_t        ci       = vcount();
        push_v(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.5f, 0.5f);
        uint32_t rs = vcount();
        for (int i = 0; i < DISC_SEG; ++i)
        {
            float a = TWO_PI * i / DISC_SEG;
            float c = cosf(a), s = sinf(a);
            push_v(c * 0.12f, s * 0.12f, 0.f, 0.f, 0.f, 1.f, c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        }
        for (int i = 0; i < DISC_SEG; ++i)
            push_tri(ci, rs + i, rs + (i + 1) % DISC_SEG);

        // 8 diamond rays (XY plane, normal +Z)
        for (int r = 0; r < 8; ++r)
        {
            float    a  = TWO_PI * r / 8;
            float    cx = cosf(a), cz = sinf(a);
            float    px = -cz, pz = cx;
            uint32_t b = vcount();
            push_v(cx * 0.13f, cz * 0.13f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
            push_v(cx * 0.20f + px * 0.035f, cz * 0.20f + pz * 0.035f, 0.f, 0.f, 0.f, 1.f, 0.5f, 0.f);
            push_v(cx * 0.27f, cz * 0.27f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
            push_v(cx * 0.20f - px * 0.035f, cz * 0.20f - pz * 0.035f, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
            push_tri(b, b + 1, b + 2);
            push_tri(b, b + 2, b + 3);
        }

        // Arrow shaft — two perpendicular flat planes for 360-degree visibility
        {
            uint32_t b = vcount();
            push_v(-0.025f, 0.f, 0.15f, 0.f, 1.f, 0.f, 0.f, 0.f);
            push_v(0.025f, 0.f, 0.15f, 0.f, 1.f, 0.f, 1.f, 0.f);
            push_v(0.025f, 0.f, 0.35f, 0.f, 1.f, 0.f, 1.f, 1.f);
            push_v(-0.025f, 0.f, 0.35f, 0.f, 1.f, 0.f, 0.f, 1.f);
            push_tri(b, b + 1, b + 2);
            push_tri(b, b + 2, b + 3);
        }
        {
            uint32_t b = vcount();
            push_v(0.f, -0.025f, 0.15f, 1.f, 0.f, 0.f, 0.f, 0.f);
            push_v(0.f, 0.025f, 0.15f, 1.f, 0.f, 0.f, 1.f, 0.f);
            push_v(0.f, 0.025f, 0.35f, 1.f, 0.f, 0.f, 1.f, 1.f);
            push_v(0.f, -0.025f, 0.35f, 1.f, 0.f, 0.f, 0.f, 1.f);
            push_tri(b, b + 1, b + 2);
            push_tri(b, b + 2, b + 3);
        }

        // Arrowhead — two perpendicular triangles
        {
            uint32_t b = vcount();
            push_v(-0.06f, 0.f, 0.32f, 0.f, 1.f, 0.f, 0.f, 0.f);
            push_v(0.06f, 0.f, 0.32f, 0.f, 1.f, 0.f, 1.f, 0.f);
            push_v(0.00f, 0.f, 0.55f, 0.f, 1.f, 0.f, 0.5f, 1.f);
            push_tri(b, b + 1, b + 2);
        }
        {
            uint32_t b = vcount();
            push_v(0.f, -0.06f, 0.32f, 1.f, 0.f, 0.f, 0.f, 0.f);
            push_v(0.f, 0.06f, 0.32f, 1.f, 0.f, 0.f, 1.f, 0.f);
            push_v(0.f, 0.00f, 0.55f, 1.f, 0.f, 0.f, 0.5f, 1.f);
            push_tri(b, b + 1, b + 2);
        }

        out_mesh.SubMeshes.init(arena, 1);
        AssetSubMesh& sub        = out_mesh.SubMeshes.push_use({});
        sub.VertexCount          = vcount();
        sub.IndexCount           = static_cast<uint32_t>(out_mesh.Indices.size());
        sub.VertexUnitStreamSize = 8 * sizeof(float);
        sub.IndexUnitStreamSize  = sizeof(uint32_t);
        sub.TotalByteSize        = sub.VertexCount * sub.VertexUnitStreamSize;
        // sub.MaterialUUID is patched by RegisterBuiltinMeshes after this returns.

        out_hierarchy.Hierarchies.init(arena, 1);
        out_hierarchy.LocalTransforms.init(arena, 1);
        out_hierarchy.GlobalTransforms.init(arena, 1);
        out_hierarchy.Names.init(arena, 1);
        out_hierarchy.MaterialNames.init(arena, 1);
        out_hierarchy.NodeNames.init(arena, 64);
        out_hierarchy.NodeMeshes.init(arena, 1);
        out_hierarchy.NodeMaterials.init(arena, 1);
        out_hierarchy.LocalTransforms.push(Identity<Mat4f>());
        out_hierarchy.GlobalTransforms.push(Identity<Mat4f>());
    }

    static void BuildDirectionalLightMaterial(AssetMaterial& mat)
    {
        // Yellow albedo + emissive self-glow.
        // Shader: color = ambient + Lo + albedo * emissive.r
        // emissive.r = 1 makes the icon always show its albedo color regardless of lighting.
        mat.AlbedoColor[0]   = 1.f;
        mat.AlbedoColor[1]   = 0.85f;
        mat.AlbedoColor[2]   = 0.f;
        mat.AlbedoColor[3]   = 1.f;
        mat.EmissiveColor[0] = 1.f; // scalar multiplier on albedo
        mat.Factors[1]       = 0.f; // metallic = 0
    }

    static constexpr BuiltinMeshEntry kBuiltinMeshTable[] = {
        {
         "ff000000-0000-0000-0000-000000000001", // mesh
        "ff000000-0000-0001-0000-000000000001", // material
        BuildDirectionalLightIcon, BuildDirectionalLightMaterial,
         },
    };

    static_assert(std::size(kBuiltinMeshTable) == static_cast<uint32_t>(BuiltinMeshID::COUNT), "kBuiltinMeshTable size must match BuiltinMeshID::COUNT");

    const char* BuiltinMeshUUID(BuiltinMeshID id)
    {
        return kBuiltinMeshTable[static_cast<uint32_t>(id)].MeshUUID;
    }

    uuids::uuid BuiltinMeshUUIDParsed(BuiltinMeshID id)
    {
        auto r = uuids::uuid::from_string(BuiltinMeshUUID(id));
        return r ? *r : uuids::uuid{};
    }

    uuids::uuid BuiltinMaterialUUIDParsed(BuiltinMeshID id)
    {
        const char* s = kBuiltinMeshTable[static_cast<uint32_t>(id)].MaterialUUID;
        if (!s)
            return {};
        auto r = uuids::uuid::from_string(s);
        return r ? *r : uuids::uuid{};
    }

    void RegisterBuiltinMeshes(ArenaAllocator* arena)
    {
        for (const auto& entry : kBuiltinMeshTable)
        {
            // Ingest material first — must exist before the mesh is rendered.
            if (entry.MaterialUUID && entry.BuildMaterial)
            {
                AssetMaterial mat{};
                mat.Name.init(arena, entry.MaterialUUID);
                auto r = uuids::uuid::from_string(entry.MaterialUUID);
                if (r)
                    mat.MaterialUUID = *r;
                entry.BuildMaterial(mat);
                Managers::AssetManager::IngestMaterial(std::move(mat));
            }

            // Build mesh then patch all submesh MaterialUUIDs before ingesting.
            AssetMesh          mesh{};
            AssetNodeHierarchy hier{};
            entry.BuildMesh(arena, mesh, hier);

            auto mesh_uuid_result = uuids::uuid::from_string(entry.MeshUUID);
            if (mesh_uuid_result)
            {
                mesh.MeshUUID          = *mesh_uuid_result;
                hier.MeshUUID          = *mesh_uuid_result;
                hier.NodeHierarchyUUID = *mesh_uuid_result;
            }

            if (entry.MaterialUUID)
            {
                auto mat_r = uuids::uuid::from_string(entry.MaterialUUID);
                if (mat_r)
                    for (auto& sub : mesh.SubMeshes)
                        sub.MaterialUUID = *mat_r;
            }

            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hier));
        }
    }

} // namespace ZEngine::Rendering

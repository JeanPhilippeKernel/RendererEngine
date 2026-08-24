#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Rendering/BuiltinMeshes.h>
#include <uuid.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Importers;

namespace ZEngine::Rendering
{
    void CreateDirectionalLightMesh(ArenaAllocator* arena, AssetMesh& out_mesh, AssetNodeHierarchy& out_hierarchy)
    {
        using namespace Core::Maths;

        // Estimated counts: disc(13v 36i) + rays(32v 48i) + arrow(14v 18i) = 59v 102i
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

        // Returns the current vertex count (index of the next vertex to be added).
        auto            vcount     = [&]() -> uint32_t { return static_cast<uint32_t>(out_mesh.Vertices.size() / 8); };

        constexpr float PI         = 3.14159265358979323846f;
        constexpr float TWO_PI     = 2.f * PI;
        constexpr int   DISC_SEGS  = 12;

        // ── Disc (12-gon in XY plane, normal +Z) ──────────────────────────────────
        uint32_t        center_idx = vcount();
        push_v(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.5f, 0.5f);

        uint32_t ring_start = vcount();
        for (int i = 0; i < DISC_SEGS; ++i)
        {
            float a = TWO_PI * i / DISC_SEGS;
            float c = cosf(a), s = sinf(a);
            push_v(c * 0.12f, s * 0.12f, 0.f, 0.f, 0.f, 1.f, c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        }
        for (int i = 0; i < DISC_SEGS; ++i)
            push_tri(center_idx, ring_start + i, ring_start + (i + 1) % DISC_SEGS);

        // ── 8 diamond rays (XY plane, normal +Z) ──────────────────────────────────
        constexpr int   RAYS       = 8;
        constexpr float RAY_INNER  = 0.13f;
        constexpr float RAY_HALF_W = 0.035f;
        constexpr float RAY_MID_R  = 0.20f;
        constexpr float RAY_TIP_R  = 0.27f;

        for (int r = 0; r < RAYS; ++r)
        {
            float    a  = TWO_PI * r / RAYS;
            float    cx = cosf(a), cz = sinf(a); // forward along ray
            float    px = -cz, pz = cx;          // perpendicular in XY plane

            uint32_t base = vcount();
            push_v(cx * RAY_INNER, cz * RAY_INNER, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);                                      // inner
            push_v(cx * RAY_MID_R + px * RAY_HALF_W, cz * RAY_MID_R + pz * RAY_HALF_W, 0.f, 0.f, 0.f, 1.f, 0.5f, 0.f); // left
            push_v(cx * RAY_TIP_R, cz * RAY_TIP_R, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);                                      // tip
            push_v(cx * RAY_MID_R - px * RAY_HALF_W, cz * RAY_MID_R - pz * RAY_HALF_W, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f); // right

            push_tri(base, base + 1, base + 2);
            push_tri(base, base + 2, base + 3);
        }

        // ── Arrow shaft — two perpendicular flat planes for 360-degree visibility ──
        // Plane 1: XZ (y = 0), normal +Y
        {
            uint32_t base = vcount();
            push_v(-0.025f, 0.f, 0.15f, 0.f, 1.f, 0.f, 0.f, 0.f);
            push_v(0.025f, 0.f, 0.15f, 0.f, 1.f, 0.f, 1.f, 0.f);
            push_v(0.025f, 0.f, 0.35f, 0.f, 1.f, 0.f, 1.f, 1.f);
            push_v(-0.025f, 0.f, 0.35f, 0.f, 1.f, 0.f, 0.f, 1.f);
            push_tri(base, base + 1, base + 2);
            push_tri(base, base + 2, base + 3);
        }
        // Plane 2: YZ (x = 0), normal +X
        {
            uint32_t base = vcount();
            push_v(0.f, -0.025f, 0.15f, 1.f, 0.f, 0.f, 0.f, 0.f);
            push_v(0.f, 0.025f, 0.15f, 1.f, 0.f, 0.f, 1.f, 0.f);
            push_v(0.f, 0.025f, 0.35f, 1.f, 0.f, 0.f, 1.f, 1.f);
            push_v(0.f, -0.025f, 0.35f, 1.f, 0.f, 0.f, 0.f, 1.f);
            push_tri(base, base + 1, base + 2);
            push_tri(base, base + 2, base + 3);
        }

        // ── Arrowhead — two perpendicular triangles ────────────────────────────────
        // Plane 1: XZ (y = 0), normal +Y
        {
            uint32_t base = vcount();
            push_v(-0.06f, 0.f, 0.32f, 0.f, 1.f, 0.f, 0.f, 0.f);
            push_v(0.06f, 0.f, 0.32f, 0.f, 1.f, 0.f, 1.f, 0.f);
            push_v(0.00f, 0.f, 0.55f, 0.f, 1.f, 0.f, 0.5f, 1.f);
            push_tri(base, base + 1, base + 2);
        }
        // Plane 2: YZ (x = 0), normal +X
        {
            uint32_t base = vcount();
            push_v(0.f, -0.06f, 0.32f, 1.f, 0.f, 0.f, 0.f, 0.f);
            push_v(0.f, 0.06f, 0.32f, 1.f, 0.f, 0.f, 1.f, 0.f);
            push_v(0.f, 0.00f, 0.55f, 1.f, 0.f, 0.f, 0.5f, 1.f);
            push_tri(base, base + 1, base + 2);
        }

        // ── SubMesh (single sub-mesh covering all geometry) ────────────────────────
        out_mesh.SubMeshes.init(arena, 1);
        AssetSubMesh& sub        = out_mesh.SubMeshes.push_use({});
        sub.VertexCount          = vcount();
        sub.IndexCount           = static_cast<uint32_t>(out_mesh.Indices.size());
        sub.VertexOffset         = 0;
        sub.IndexOffset          = 0;
        sub.VertexUnitStreamSize = 8 * sizeof(float);
        sub.IndexUnitStreamSize  = sizeof(uint32_t);
        sub.TotalByteSize        = sub.VertexCount * sub.VertexUnitStreamSize;

        // ── UUID ───────────────────────────────────────────────────────────────────
        auto uuid_result         = uuids::uuid::from_string(DIRECTIONAL_LIGHT_MESH_UUID);
        if (uuid_result)
            out_mesh.MeshUUID = *uuid_result;

        // ── Minimal node hierarchy (one root node, no children) ────────────────────
        out_hierarchy.MeshUUID          = out_mesh.MeshUUID;
        out_hierarchy.NodeHierarchyUUID = out_mesh.MeshUUID;

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
} // namespace ZEngine::Rendering

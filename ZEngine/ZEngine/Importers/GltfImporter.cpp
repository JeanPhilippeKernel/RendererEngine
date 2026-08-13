#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/ZEngineDef.h>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <uuid.h>
#include <filesystem>
#include <random>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Helpers;
using namespace uuids;

namespace ZEngine::Importers
{
    static Mat4f ToMat4(const fastgltf::math::fmat4x4& m)
    {
        Mat4f out;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out(r, c) = m.col(c)[r];
        return out;
    }

    static Mat4f NodeLocalTransform(const fastgltf::Node& node)
    {
        return std::visit(
            fastgltf::visitor{
            [](const fastgltf::math::fmat4x4& mat) { return ToMat4(mat); },
            [](const fastgltf::TRS& trs) {
                const auto& t = trs.translation;
                const auto& r = trs.rotation;
                const auto& s = trs.scale;

                // scale
                Mat4f       S = Identity<Mat4f>();
                S(0, 0)       = s.x();
                S(1, 1)       = s.y();
                S(2, 2)       = s.z();

                // rotation (quaternion qx qy qz qw)
                float qx = r.x(), qy = r.y(), qz = r.z(), qw = r.w();
                Mat4f R = Identity<Mat4f>();
                R(0, 0) = 1 - 2 * (qy * qy + qz * qz);
                R(0, 1) = 2 * (qx * qy - qz * qw);
                R(0, 2) = 2 * (qx * qz + qy * qw);
                R(1, 0) = 2 * (qx * qy + qz * qw);
                R(1, 1) = 1 - 2 * (qx * qx + qz * qz);
                R(1, 2) = 2 * (qy * qz - qx * qw);
                R(2, 0) = 2 * (qx * qz - qy * qw);
                R(2, 1) = 2 * (qy * qz + qx * qw);
                R(2, 2) = 1 - 2 * (qx * qx + qy * qy);

                // translation
                Mat4f T = Identity<Mat4f>();
                T(0, 3) = t.x();
                T(1, 3) = t.y();
                T(2, 3) = t.z();

                return T * R * S;
            },
            },
            node.transform);
    }

    // ----- mesh extraction -----

    static void ExtractMeshes(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, AssetMesh& out)
    {
        uint32_t total_verts   = 0;
        uint32_t total_indices = 0;
        uint32_t total_prims   = 0;

        for (const auto& mesh : asset.meshes)
        {
            for (const auto& prim : mesh.primitives)
            {
                if (prim.type != fastgltf::PrimitiveType::Triangles)
                    continue;
                auto it = prim.findAttribute("POSITION");
                if (it != prim.attributes.end())
                    total_verts += (uint32_t) asset.accessors[it->accessorIndex].count;
                if (prim.indicesAccessor.has_value())
                    total_indices += (uint32_t) asset.accessors[prim.indicesAccessor.value()].count;
                else
                    total_indices += (uint32_t) asset.accessors[prim.findAttribute("POSITION")->accessorIndex].count;
                ++total_prims;
            }
        }

        out.SubMeshes.init(arena, total_prims, total_prims);
        out.Vertices.init(arena, total_verts * 8); // pos(3) + nrm(3) + uv(2)
        out.Indices.init(arena, total_indices);

        uint32_t vertex_offset = 0;
        uint32_t index_offset  = 0;
        uint32_t sub_idx       = 0;

        for (const auto& mesh : asset.meshes)
        {
            for (const auto& prim : mesh.primitives)
            {
                if (prim.type != fastgltf::PrimitiveType::Triangles)
                    continue;

                auto pos_it = prim.findAttribute("POSITION");
                auto nrm_it = prim.findAttribute("NORMAL");
                auto uv_it  = prim.findAttribute("TEXCOORD_0");

                if (pos_it == prim.attributes.end())
                    continue;

                const auto& pos_acc = asset.accessors[pos_it->accessorIndex];
                uint32_t    vc      = (uint32_t) pos_acc.count;

                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, pos_acc, [&](fastgltf::math::fvec3 pos, std::size_t) {
                    out.Vertices.push(pos.x());
                    out.Vertices.push(pos.y());
                    out.Vertices.push(pos.z());
                    out.Vertices.push(0.f); // nrm placeholder
                    out.Vertices.push(1.f);
                    out.Vertices.push(0.f);
                    out.Vertices.push(0.f); // uv placeholder
                    out.Vertices.push(0.f);
                });

                // Overwrite normals if present
                if (nrm_it != prim.attributes.end())
                {
                    const auto& nrm_acc    = asset.accessors[nrm_it->accessorIndex];
                    uint32_t    base_float = vertex_offset * 8 + 3; // float index of first normal
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, nrm_acc, [&](fastgltf::math::fvec3 n, std::size_t i) {
                        out.Vertices[base_float + i * 8 + 0] = n.x();
                        out.Vertices[base_float + i * 8 + 1] = n.y();
                        out.Vertices[base_float + i * 8 + 2] = n.z();
                    });
                }

                // Overwrite UVs if present
                if (uv_it != prim.attributes.end())
                {
                    const auto& uv_acc     = asset.accessors[uv_it->accessorIndex];
                    uint32_t    base_float = vertex_offset * 8 + 6;
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, uv_acc, [&](fastgltf::math::fvec2 uv, std::size_t i) {
                        out.Vertices[base_float + i * 8 + 0] = uv.x();
                        out.Vertices[base_float + i * 8 + 1] = uv.y();
                    });
                }

                uint32_t ic = 0;
                if (prim.indicesAccessor.has_value())
                {
                    const auto& idx_acc = asset.accessors[prim.indicesAccessor.value()];
                    ic                  = (uint32_t) idx_acc.count;
                    fastgltf::iterateAccessorWithIndex<uint32_t>(asset, idx_acc, [&](uint32_t idx, std::size_t) { out.Indices.push(idx); });
                }
                else
                {
                    ic = vc;
                    for (uint32_t i = 0; i < ic; ++i)
                        out.Indices.push(i);
                }

                AssetSubMesh& sub         = out.SubMeshes[sub_idx++];
                sub.VertexCount           = vc;
                sub.VertexOffset          = vertex_offset;
                sub.IndexCount            = ic;
                sub.IndexOffset           = index_offset;
                sub.VertexUnitStreamSize  = sizeof(float) * 8;
                sub.IndexUnitStreamSize   = sizeof(uint32_t);
                sub.StreamOffset          = sub.VertexUnitStreamSize * vertex_offset;
                sub.IndexStreamOffset     = sub.IndexUnitStreamSize * index_offset;
                sub.TotalByteSize         = vc * sub.VertexUnitStreamSize + ic * sub.IndexUnitStreamSize;

                vertex_offset            += vc;
                index_offset             += ic;
            }
        }
    }

    // ----- material extraction -----

    static void ExtractMaterials(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, uuid_random_generator& gen, Array<AssetMaterial>& out)
    {
        uint32_t n = (uint32_t) asset.materials.size();
        out.init(arena, n, n);

        for (uint32_t m = 0; m < n; ++m)
        {
            const auto&    src = asset.materials[m];
            AssetMaterial& dst = out[m];
            dst.MaterialUUID   = gen();
            dst.Name.init(arena, src.name.empty() ? "material" : src.name.data());

            {
                const auto& pbr       = src.pbrData;
                dst.AlbedoColor[0]    = pbr.baseColorFactor.x();
                dst.AlbedoColor[1]    = pbr.baseColorFactor.y();
                dst.AlbedoColor[2]    = pbr.baseColorFactor.z();
                dst.AlbedoColor[3]    = pbr.baseColorFactor.w();
                dst.RoughnessColor[0] = pbr.metallicFactor;
                dst.RoughnessColor[1] = pbr.roughnessFactor;
            }

            dst.EmissiveColor[0] = src.emissiveFactor.x();
            dst.EmissiveColor[1] = src.emissiveFactor.y();
            dst.EmissiveColor[2] = src.emissiveFactor.z();
            dst.EmissiveColor[3] = 1.f;

            if (src.alphaMode == fastgltf::AlphaMode::Blend)
                dst.Factors[2] = 0.5f;
        }
    }

    // ----- texture extraction -----

    static void ExtractTextures(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, uuid_random_generator& gen, Array<AssetTexture>& out_tex, Array<AssetMaterial>& mats)
    {
        uint32_t n = (uint32_t) asset.textures.size();
        out_tex.init(arena, n, n);

        for (uint32_t t = 0; t < n; ++t)
        {
            out_tex[t].TextureUUID = gen();
            const auto& src_tex    = asset.textures[t];
            if (src_tex.imageIndex.has_value())
            {
                const auto& img = asset.images[src_tex.imageIndex.value()];
                if (!img.name.empty())
                {
                    out_tex[t].Path.init(arena, img.name.data());
                }
                else if (const auto* uri_src = std::get_if<fastgltf::sources::URI>(&img.data))
                {
                    // string_view — copy to null-terminated buffer
                    auto   sv       = uri_src->uri.string();
                    char   tmp[512] = {};
                    size_t n        = sv.size() < 511 ? sv.size() : 511;
                    Helpers::secure_memcpy(tmp, sizeof(tmp), sv.data(), n);
                    out_tex[t].Path.init(arena, tmp);
                }
            }
        }

        // Wire UUIDs into materials
        auto tex_uuid = [&](const std::optional<fastgltf::TextureInfo>& info) -> uuids::uuid {
            if (!info.has_value())
                return {};
            uint32_t idx = (uint32_t) info->textureIndex;
            return idx < out_tex.size() ? out_tex[idx].TextureUUID : uuids::uuid{};
        };
        auto norm_uuid = [&](const std::optional<fastgltf::NormalTextureInfo>& info) -> uuids::uuid {
            if (!info.has_value())
                return {};
            uint32_t idx = (uint32_t) info->textureIndex;
            return idx < out_tex.size() ? out_tex[idx].TextureUUID : uuids::uuid{};
        };
        auto occ_uuid = [&](const std::optional<fastgltf::OcclusionTextureInfo>& info) -> uuids::uuid {
            if (!info.has_value())
                return {};
            uint32_t idx = (uint32_t) info->textureIndex;
            return idx < out_tex.size() ? out_tex[idx].TextureUUID : uuids::uuid{};
        };

        for (uint32_t m = 0; m < (uint32_t) asset.materials.size(); ++m)
        {
            const auto&    src = asset.materials[m];
            AssetMaterial& dst = mats[m];

            {
                dst.AlbedoTexUUID   = tex_uuid(src.pbrData.baseColorTexture);
                dst.SpecularTexUUID = tex_uuid(src.pbrData.metallicRoughnessTexture);
            }
            dst.EmissiveTexUUID = tex_uuid(src.emissiveTexture);
            dst.NormalTexUUID   = norm_uuid(src.normalTexture);
            dst.OpacityTexUUID  = occ_uuid(src.occlusionTexture);
        }
    }

    // ----- hierarchy extraction -----

    static void TraverseNode(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, std::size_t node_index, AssetNodeHierarchy& hier, const Array<AssetMaterial>& mats, int parent_id, int depth)
    {
        const fastgltf::Node& node = asset.nodes[node_index];
        int                   id   = AddNode(hier, parent_id, depth);
        hier.NodeNames[id]         = (uint32_t) hier.Names.size();
        auto& name                 = hier.Names.push_use({});
        name.init(arena, node.name.empty() ? "<node>" : node.name.c_str());

        hier.LocalTransforms[id]  = NodeLocalTransform(node);
        hier.GlobalTransforms[id] = Identity<Mat4f>();

        if (node.meshIndex.has_value())
        {
            std::size_t mesh_idx = node.meshIndex.value();
            const auto& mesh     = asset.meshes[mesh_idx];
            for (std::size_t p = 0; p < mesh.primitives.size(); ++p)
            {
                int sub_id             = AddNode(hier, id, depth + 1);
                hier.NodeNames[sub_id] = (uint32_t) hier.Names.size();
                auto& sub_name         = hier.Names.push_use({});
                sub_name.init(arena, mesh.name.empty() ? "<mesh>" : mesh.name.c_str());

                hier.NodeMeshes[sub_id]       = (uint32_t) mesh_idx;
                hier.LocalTransforms[sub_id]  = Identity<Mat4f>();
                hier.GlobalTransforms[sub_id] = Identity<Mat4f>();

                if (mesh.primitives[p].materialIndex.has_value())
                {
                    uint32_t mat_idx           = (uint32_t) mesh.primitives[p].materialIndex.value();
                    hier.NodeMaterials[sub_id] = mat_idx;
                    if (mat_idx < mats.size())
                    {
                        AssetMesh dummy{};
                        // wire material UUID into the corresponding sub-mesh
                        // (done outside traverse in BuildHierarchy below)
                        (void) dummy;
                    }
                }
            }
        }

        for (std::size_t child : node.children)
            TraverseNode(arena, asset, child, hier, mats, id, depth + 1);
    }

    static void BuildHierarchy(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, uuid_random_generator& gen, AssetNodeHierarchy& hier, AssetMesh& mesh, const Array<AssetMaterial>& mats)
    {
        hier.NodeHierarchyUUID = gen();
        hier.MeshUUID          = mesh.MeshUUID;

        uint32_t cap           = 3000;
        hier.Hierarchies.init(arena, cap);
        hier.LocalTransforms.init(arena, cap);
        hier.GlobalTransforms.init(arena, cap);
        hier.Names.init(arena, cap);
        hier.NodeNames.init(arena, cap);
        hier.NodeMeshes.init(arena, cap);
        hier.NodeMaterials.init(arena, cap);
        hier.MaterialNames.init(arena, (uint32_t) mats.size(), (uint32_t) mats.size());
        for (uint32_t m = 0; m < (uint32_t) mats.size(); ++m)
            hier.MaterialNames[m].init(arena, mats[m].Name.c_str());

        if (asset.defaultScene.has_value())
        {
            for (std::size_t n : asset.scenes[asset.defaultScene.value()].nodeIndices)
                TraverseNode(arena, asset, n, hier, mats, -1, 0);
        }
        else
        {
            for (const auto& scene : asset.scenes)
                for (std::size_t n : scene.nodeIndices)
                    TraverseNode(arena, asset, n, hier, mats, -1, 0);
        }

        // Wire material UUIDs into sub-meshes
        uint32_t sub_idx = 0;
        for (std::size_t mi = 0; mi < asset.meshes.size(); ++mi)
        {
            const auto& fmesh = asset.meshes[mi];
            for (std::size_t p = 0; p < fmesh.primitives.size(); ++p)
            {
                if (sub_idx >= mesh.SubMeshes.size())
                    break;
                if (fmesh.primitives[p].materialIndex.has_value())
                {
                    uint32_t mat_idx = (uint32_t) fmesh.primitives[p].materialIndex.value();
                    if (mat_idx < mats.size())
                        mesh.SubMeshes[sub_idx].MaterialUUID = mats[mat_idx].MaterialUUID;
                }
                ++sub_idx;
            }
        }
    }

    // ----- importer interface -----

    void GltfImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(64), &Arena);
    }

    bool GltfImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        return secure_strcmp(extension, "glb") == 0 || secure_strcmp(extension, "gltf") == 0;
    }

    Core::VFS::VFSResult<void> GltfImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        // Resolve VFS path to native filesystem path
        char        native[MAX_FILE_PATH_COUNT] = {};
        const char* ws                          = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
        if (ws && ws[0] != '\0')
            path.ResolveNative(ws, native, sizeof(native));
        else
            path.ToNative(native, sizeof(native));

        fastgltf::Parser parser;
        auto             fs_path = std::filesystem::path(native);
        auto             buf     = fastgltf::GltfDataBuffer::FromPath(fs_path);
        if (buf.error() != fastgltf::Error::None)
        {
            ZENGINE_CORE_ERROR("[GltfImporter] Failed to read file '{}': {}", native, fastgltf::getErrorMessage(buf.error()))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        auto result = parser.loadGltf(buf.get(), fs_path.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices);

        if (result.error() != fastgltf::Error::None)
        {
            ZENGINE_CORE_ERROR("[GltfImporter] Parse error for '{}': {}", native, fastgltf::getErrorMessage(result.error()))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        fastgltf::Asset&             asset = result.get();

        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(32), &scratch);

        std::random_device    rd;
        std::mt19937          generator(rd());
        uuid_random_generator gen(&generator);

        AssetMesh             mesh      = {};
        AssetNodeHierarchy    hierarchy = {};
        Array<AssetMaterial>  materials = {};
        Array<AssetTexture>   textures  = {};

        ExtractMeshes(&scratch, asset, mesh);
        mesh.MeshUUID = meta.AssetUUID;

        ExtractMaterials(&scratch, asset, gen, materials);
        ExtractTextures(&scratch, asset, gen, textures, materials);
        BuildHierarchy(&scratch, asset, gen, hierarchy, mesh, materials);

        auto* mgr = Managers::AssetManager::Instance();
        if (mgr)
        {
            // Skip re-ingest if already loaded. Multi-instance support deferred to RRM.
            auto* reg = mgr->Registry;
            if (reg)
            {
                const auto* existing = reg->FindByUUID(meta.AssetUUID);
                if (existing && existing->State == Core::VFS::AssetState::Loaded)
                {
                    Arena.Clear();
                    return Core::VFS::VFSResult<void>::Ok();
                }
            }

            Managers::AssetManager::IngestTextures(std::move(textures));
            for (size_t i = 0; i < materials.size(); ++i)
                Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
        }

        Arena.Clear();
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers

#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Importers/MeshOptimizer.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/ZEngineDef.h>
#include <fastgltf/base64.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fmt/format.h>
#include <uuid.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <random>
#include <vector>

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

    static bool HasTriangleGeometry(const fastgltf::Primitive& primitive)
    {
        return primitive.type == fastgltf::PrimitiveType::Triangles && primitive.findAttribute("POSITION") != primitive.attributes.end();
    }

    static ArrayView<const uint8_t> BufferViewBytes(const fastgltf::Asset& asset, size_t index)
    {
        if (index >= asset.bufferViews.size())
            return {};
        const auto& view = asset.bufferViews[index];
        if (view.bufferIndex >= asset.buffers.size())
            return {};
        const auto&              buffer = asset.buffers[view.bufferIndex];
        ArrayView<const uint8_t> bytes;
        if (const auto* array = std::get_if<fastgltf::sources::Array>(&buffer.data))
            bytes = {reinterpret_cast<const uint8_t*>(array->bytes.data()), array->bytes.size()};
        else if (const auto* byte_view = std::get_if<fastgltf::sources::ByteView>(&buffer.data))
            bytes = {reinterpret_cast<const uint8_t*>(byte_view->bytes.data()), byte_view->bytes.size()};
        const size_t available = std::min(bytes.size(), buffer.byteLength);
        if (!bytes.data() || view.byteOffset > available || view.byteLength > available - view.byteOffset)
            return {};
        return {bytes.data() + view.byteOffset, view.byteLength};
    }

    static bool HasAccessorRange(const fastgltf::Asset& asset, size_t view_index, size_t offset, size_t count, size_t element_size, size_t stride)
    {
        const auto bytes = BufferViewBytes(asset, view_index);
        if (!count || !element_size || stride < element_size || offset > bytes.size() || element_size > bytes.size() - offset)
            return false;
        // Division avoids overflow in offset + (count - 1) * stride + size.
        return count - 1 <= (bytes.size() - offset - element_size) / stride;
    }

    static bool HasValidAccessor(const fastgltf::Asset& asset, size_t index, fastgltf::AccessorType type)
    {
        if (index >= asset.accessors.size())
            return false;
        const auto& accessor     = asset.accessors[index];
        const auto  element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
        if (accessor.type != type || !accessor.count || !element_size || accessor.count > std::numeric_limits<uint32_t>::max())
            return false;
        if (accessor.bufferViewIndex)
        {
            const auto view = *accessor.bufferViewIndex;
            if (view >= asset.bufferViews.size() || !HasAccessorRange(asset, view, accessor.byteOffset, accessor.count, element_size, asset.bufferViews[view].byteStride.value_or(element_size)))
                return false;
        }
        else if (accessor.byteOffset != 0)
            return false;
        if (accessor.sparse)
        {
            const auto& sparse = *accessor.sparse;
            if (sparse.count > accessor.count || (sparse.indexComponentType != fastgltf::ComponentType::UnsignedByte && sparse.indexComponentType != fastgltf::ComponentType::UnsignedShort && sparse.indexComponentType != fastgltf::ComponentType::UnsignedInt))
                return false;
            const auto index_size = fastgltf::getComponentByteSize(sparse.indexComponentType);
            if (!HasAccessorRange(asset, sparse.indicesBufferView, sparse.indicesByteOffset, sparse.count, index_size, index_size) || !HasAccessorRange(asset, sparse.valuesBufferView, sparse.valuesByteOffset, sparse.count, element_size, element_size))
                return false;
        }
        return true;
    }

    static bool HasValidGltfData(const fastgltf::Asset& asset)
    {
        size_t vertex_count = 0, index_count = 0;
        for (const auto& mesh : asset.meshes)
            for (const auto& primitive : mesh.primitives)
            {
                if (!HasTriangleGeometry(primitive))
                    continue;
                const auto position = primitive.findAttribute("POSITION")->accessorIndex;
                if (!HasValidAccessor(asset, position, fastgltf::AccessorType::Vec3))
                    return false;
                const auto count = asset.accessors[position].count;
                for (const auto& attribute : primitive.attributes)
                {
                    const auto type = attribute.name == "NORMAL" ? fastgltf::AccessorType::Vec3 : fastgltf::AccessorType::Vec2;
                    if (attribute.name != "NORMAL" && attribute.name != "TEXCOORD_0")
                        continue;
                    if (!HasValidAccessor(asset, attribute.accessorIndex, type) || asset.accessors[attribute.accessorIndex].count != count)
                        return false;
                }
                size_t indices = count;
                if (primitive.indicesAccessor)
                {
                    if (!HasValidAccessor(asset, *primitive.indicesAccessor, fastgltf::AccessorType::Scalar))
                        return false;
                    const auto& accessor = asset.accessors[*primitive.indicesAccessor];
                    if (accessor.componentType != fastgltf::ComponentType::UnsignedByte && accessor.componentType != fastgltf::ComponentType::UnsignedShort && accessor.componentType != fastgltf::ComponentType::UnsignedInt)
                        return false;
                    indices = accessor.count;
                }
                if (indices % 3 != 0 || count > std::numeric_limits<uint32_t>::max() / 8 - vertex_count || indices > std::numeric_limits<uint32_t>::max() - index_count)
                    return false;
                vertex_count += count;
                index_count  += indices;
            }
        for (const auto& texture : asset.textures)
            if (texture.imageIndex)
            {
                if (*texture.imageIndex >= asset.images.size())
                    return false;
                if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&asset.images[*texture.imageIndex].data); view && BufferViewBytes(asset, view->bufferViewIndex).size() == 0)
                    return false;
            }
        return true;
    }

    static bool HasValidNodeGraph(const fastgltf::Asset& asset)
    {
        if (asset.defaultScene && *asset.defaultScene >= asset.scenes.size())
            return false;
        std::vector<size_t> parents(asset.nodes.size());
        for (const auto& node : asset.nodes)
        {
            if (node.meshIndex && *node.meshIndex >= asset.meshes.size())
                return false;
            for (auto child : node.children)
                if (child >= asset.nodes.size() || ++parents[child] > 1)
                    return false; // glTF nodes form trees, not shared-child DAGs.
        }
        for (const auto& scene : asset.scenes)
        {
            std::vector<bool> roots(asset.nodes.size());
            for (auto root : scene.nodeIndices)
            {
                if (root >= asset.nodes.size() || parents[root] != 0 || roots[root])
                    return false;
                roots[root] = true;
            }
        }
        // Topological traversal detects cycles, including disconnected nodes,
        // without recursing on untrusted scene depth.
        std::vector<size_t> nodes;
        for (size_t i = 0; i < parents.size(); ++i)
            if (parents[i] == 0)
                nodes.push_back(i);
        for (size_t i = 0; i < nodes.size(); ++i)
            for (auto child : asset.nodes[nodes[i]].children)
                if (--parents[child] == 0)
                    nodes.push_back(child);
        return nodes.size() == asset.nodes.size();
    }

    static bool FitsScratch(const fastgltf::Asset& asset, size_t available, bool import_materials, bool import_textures, size_t& hierarchy_capacity)
    {
        // Upper bound every arena allocation, including hash-table rounding and
        // copied strings. Keep the existing bounded scratch policy, but fail
        // before custom containers can dereference a failed allocation.
        auto consume = [&](size_t count, size_t width) {
            if (count > available / width)
                return false;
            available -= count * width;
            return true;
        };
        using NodeMap = UnorderedHashMap<uint32_t, uint32_t>;
        if (!consume(1, 1024 + 3 * 16 * sizeof(NodeMap::Entry)))
            return false; // minimum map capacities and allocation alignment
        for (const auto& mesh : asset.meshes)
            for (const auto& primitive : mesh.primitives)
                if (HasTriangleGeometry(primitive))
                {
                    const auto vertices = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex].count;
                    const auto indices  = primitive.indicesAccessor ? asset.accessors[*primitive.indicesAccessor].count : vertices;
                    if (!consume(vertices, 8 * sizeof(float)) || !consume(indices, sizeof(uint32_t)) || !consume(1, sizeof(AssetSubMesh)))
                        return false;
                }
        if (import_materials)
        {
            if (!consume(asset.materials.size(), sizeof(AssetMaterial) + sizeof(String) + (import_textures ? 5 * MAX_FILE_PATH_COUNT : 0)))
                return false;
            for (const auto& material : asset.materials)
                if (!consume(2, std::max(material.name.size(), size_t(8)) + 1))
                    return false;
            if (import_textures)
            {
                if (!consume(asset.textures.size(), sizeof(AssetTexture) + MAX_FILE_PATH_COUNT + 512))
                    return false;
                for (const auto& texture : asset.textures)
                    if (texture.imageIndex && !consume(1, asset.images[*texture.imageIndex].name.size() + 1))
                        return false;
            }
        }
        const size_t node_bytes = sizeof(Helpers::NodeHierarchy) + 2 * sizeof(Mat4f) + sizeof(String) + 3 * 4 * sizeof(NodeMap::Entry);
        hierarchy_capacity      = 0;
        std::vector<size_t> pending;
        auto                count_scene = [&](const fastgltf::Scene& scene) {
            pending.assign(scene.nodeIndices.begin(), scene.nodeIndices.end());
            while (!pending.empty())
            {
                const auto& node = asset.nodes[pending.back()];
                pending.pop_back();
                if (!consume(1, node_bytes) || !consume(1, std::max(node.name.size(), size_t(6)) + 1))
                    return false;
                ++hierarchy_capacity;
                if (node.meshIndex)
                {
                    const auto& mesh = asset.meshes[*node.meshIndex];
                    for (const auto& primitive : mesh.primitives)
                        if (HasTriangleGeometry(primitive))
                        {
                            if (!consume(1, node_bytes) || !consume(1, std::max(mesh.name.size(), size_t(6)) + 1))
                                return false;
                            ++hierarchy_capacity;
                        }
                }
                pending.insert(pending.end(), node.children.begin(), node.children.end());
            }
            return true;
        };
        if (asset.defaultScene)
            return count_scene(asset.scenes[*asset.defaultScene]);
        for (const auto& scene : asset.scenes)
            if (!count_scene(scene))
                return false;
        return true;
    }

    static bool ExtractMeshes(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, AssetMesh& out)
    {
        uint32_t total_verts   = 0;
        uint32_t total_indices = 0;
        uint32_t total_prims   = 0;

        for (const auto& mesh : asset.meshes)
        {
            for (const auto& prim : mesh.primitives)
            {
                if (!HasTriangleGeometry(prim))
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
                if (!HasTriangleGeometry(prim))
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
                    bool valid_indices  = true;
                    fastgltf::iterateAccessorWithIndex<uint32_t>(asset, idx_acc, [&](uint32_t idx, std::size_t) {
                        valid_indices = valid_indices && idx < vc;
                        out.Indices.push(idx);
                    });
                    if (!valid_indices)
                        return false;
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
        return true;
    }

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

    static void TraverseNodes(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, const fastgltf::Scene& scene, AssetNodeHierarchy& hier, const Array<AssetMaterial>& mats, const std::vector<uint32_t>& mesh_offsets)
    {
        struct PendingNode
        {
            size_t Index;
            int    Parent;
            int    Depth;
        };
        std::vector<PendingNode> pending;
        for (auto root = scene.nodeIndices.rbegin(); root != scene.nodeIndices.rend(); ++root)
            pending.push_back({*root, -1, 0});
        while (!pending.empty())
        {
            const auto [node_index, parent_id, depth] = pending.back();
            pending.pop_back();
            const fastgltf::Node& node = asset.nodes[node_index];
            int                   id   = AddNode(hier, parent_id, depth);
            hier.NodeNames[id]         = (uint32_t) hier.Names.size();
            auto& name                 = hier.Names.push_use({});
            name.init(arena, node.name.empty() ? "<node>" : node.name.c_str());

            hier.LocalTransforms[id]  = NodeLocalTransform(node);
            hier.GlobalTransforms[id] = Identity<Mat4f>();

            if (node.meshIndex.has_value())
            {
                std::size_t mesh_idx      = node.meshIndex.value();
                const auto& mesh          = asset.meshes[mesh_idx];
                uint32_t    submesh_index = mesh_offsets[mesh_idx];
                for (std::size_t p = 0; p < mesh.primitives.size(); ++p)
                {
                    if (!HasTriangleGeometry(mesh.primitives[p]))
                        continue;
                    int sub_id             = AddNode(hier, id, depth + 1);
                    hier.NodeNames[sub_id] = (uint32_t) hier.Names.size();
                    auto& sub_name         = hier.Names.push_use({});
                    sub_name.init(arena, mesh.name.empty() ? "<mesh>" : mesh.name.c_str());

                    hier.NodeMeshes[sub_id]       = submesh_index++;
                    hier.LocalTransforms[sub_id]  = Identity<Mat4f>();
                    hier.GlobalTransforms[sub_id] = Identity<Mat4f>();

                    if (mesh.primitives[p].materialIndex.has_value())
                    {
                        uint32_t mat_idx = (uint32_t) mesh.primitives[p].materialIndex.value();
                        if (mat_idx < mats.size())
                            hier.NodeMaterials[sub_id] = mat_idx;
                    }
                }
            }

            for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                pending.push_back({*child, id, depth + 1});
        }
    }

    static void BuildHierarchy(Core::Memory::ArenaAllocator* arena, const fastgltf::Asset& asset, uuid_random_generator& gen, AssetNodeHierarchy& hier, AssetMesh& mesh, const Array<AssetMaterial>& mats, size_t capacity)
    {
        hier.NodeHierarchyUUID = gen();
        hier.MeshUUID          = mesh.MeshUUID;

        hier.Hierarchies.init(arena, capacity);
        hier.LocalTransforms.init(arena, capacity);
        hier.GlobalTransforms.init(arena, capacity);
        hier.Names.init(arena, capacity);
        hier.NodeNames.init(arena, 2 * capacity);
        hier.NodeMeshes.init(arena, 2 * capacity);
        hier.NodeMaterials.init(arena, 2 * capacity);
        hier.MaterialNames.init(arena, (uint32_t) mats.size(), (uint32_t) mats.size());
        for (uint32_t m = 0; m < (uint32_t) mats.size(); ++m)
            hier.MaterialNames[m].init(arena, mats[m].Name.c_str());

        // Use the same primitive filtering as extraction. These offsets also map
        // hierarchy nodes to flattened submeshes across multi-primitive meshes.
        std::vector<uint32_t> mesh_offsets(asset.meshes.size());
        uint32_t              sub_idx = 0;
        for (std::size_t mi = 0; mi < asset.meshes.size(); ++mi)
        {
            const auto& fmesh = asset.meshes[mi];
            mesh_offsets[mi]  = sub_idx;
            for (std::size_t p = 0; p < fmesh.primitives.size(); ++p)
            {
                if (!HasTriangleGeometry(fmesh.primitives[p]))
                    continue;
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
        if (asset.defaultScene.has_value())
        {
            TraverseNodes(arena, asset, asset.scenes[*asset.defaultScene], hier, mats, mesh_offsets);
        }
        else
        {
            for (const auto& scene : asset.scenes)
                TraverseNodes(arena, asset, scene, hier, mats, mesh_offsets);
        }
    }

    void GltfImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(64), &Arena, "ImportPipeline/GltfImporter");
    }

    bool GltfImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        return secure_strcmp(extension, "glb") == 0 || secure_strcmp(extension, "gltf") == 0;
    }

    Core::VFS::VFSResult<void> GltfImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        std::lock_guard import_lock(m_import_mutex);
        // Resolve VFS path to native filesystem path
        char            native[MAX_FILE_PATH_COUNT] = {};
        const char*     ws                          = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
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

        auto result = parser.loadGltf(buf.get(), fs_path.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);

        if (result.error() != fastgltf::Error::None)
        {
            ZENGINE_CORE_ERROR("[GltfImporter] Parse error for '{}': {}", native, fastgltf::getErrorMessage(result.error()))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        fastgltf::Asset& asset = result.get();
        if (!HasValidGltfData(asset) || !HasValidNodeGraph(asset))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);

        size_t hierarchy_capacity = 0;
        if (!FitsScratch(asset, ZMega(32), true, true, hierarchy_capacity))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(32), &scratch, "ImportPipeline/GltfImporter/RuntimeScratch");
        if (!scratch.Allocate(ZMega(32)))
        {
            Arena.Clear();
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
        }
        scratch.Clear();

        std::random_device    rd;
        std::mt19937          generator(rd());
        uuid_random_generator gen(&generator);

        AssetMesh             mesh      = {};
        AssetNodeHierarchy    hierarchy = {};
        Array<AssetMaterial>  materials = {};
        Array<AssetTexture>   textures  = {};

        if (!ExtractMeshes(&scratch, asset, mesh))
        {
            Arena.Clear();
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        mesh.MeshUUID = meta.AssetUUID;

        ExtractMaterials(&scratch, asset, gen, materials);
        ExtractTextures(&scratch, asset, gen, textures, materials);
        BuildHierarchy(&scratch, asset, gen, hierarchy, mesh, materials, hierarchy_capacity);

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

    void GltfImporter::ImportFile(const char* filename, const AssetCodec::ImportConfiguration& cfg, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log)
    {
        std::lock_guard import_lock(m_import_mutex);
        auto            mesh_path = AssetCodec::ValidateImportConfiguration(cfg);
        if (mesh_path.Failed() || !filename || !filename[0])
        {
            if (on_error)
                on_error(context, fmt::format("Invalid import configuration (VFS error {})", static_cast<uint32_t>(mesh_path.Failed() ? mesh_path.Error() : Core::VFS::VFSError::InvalidPath)));
            return;
        }
        // The caller's arena is sized only for a few short path strings (#760) — carve a
        // scratch sub-arena from this importer's own, generously-sized private Arena
        // instead, matching the pattern Import() already uses for hot-reload.
        // Intermediate geometry uses this scratch arena; output paths own their
        // strings independently and remain valid throughout the completion callback.
        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(32), &scratch, "ImportPipeline/GltfImporter/EditorScratch");
        arena               = &scratch;

        // Config remains borrowed throughout this synchronous call; no scratch copy.
        const auto& config  = cfg;

        auto        fs_path = std::filesystem::path(filename);
        auto        buf     = fastgltf::GltfDataBuffer::FromPath(fs_path);
        if (buf.error() != fastgltf::Error::None)
        {
            if (on_error)
                on_error(context, fastgltf::getErrorMessage(buf.error()));
            Arena.Clear();
            return;
        }

        if (on_progress)
            on_progress(context, 0.1f);

        fastgltf::Parser parser;
        auto             result = parser.loadGltf(buf.get(), fs_path.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);

        if (result.error() != fastgltf::Error::None)
        {
            if (on_error)
                on_error(context, fastgltf::getErrorMessage(result.error()));
            Arena.Clear();
            return;
        }

        if (on_progress)
            on_progress(context, 0.3f);

        fastgltf::Asset& asset = result.get();
        if (!HasValidGltfData(asset) || !HasValidNodeGraph(asset))
        {
            if (on_error)
                on_error(context, "Invalid glTF buffer range, accessor, triangle count, or node graph");
            Arena.Clear();
            return;
        }

        size_t hierarchy_capacity = 0;
        if (!FitsScratch(asset, scratch.m_total_size, config.Options.ImportMaterials, config.Options.ImportMaterials && config.Options.ImportTextures, hierarchy_capacity) || !scratch.Allocate(scratch.m_total_size))
        {
            if (on_error)
                on_error(context, "glTF exceeds the importer scratch memory budget or memory could not be committed");
            Arena.Clear();
            return;
        }
        scratch.Clear();

        std::random_device    rd;
        std::mt19937          gen_mt(rd());
        uuid_random_generator gen(&gen_mt);

        AssetMesh             mesh      = {};
        AssetNodeHierarchy    hierarchy = {};
        Array<AssetMaterial>  materials = {};
        Array<AssetTexture>   textures  = {};

        if (!ExtractMeshes(&scratch, asset, mesh))
        {
            if (on_error)
                on_error(context, "Invalid glTF triangle vertex index");
            Arena.Clear();
            return;
        }
        mesh.MeshUUID = gen();

        // Stabilize the mesh UUID (#762): re-importing the same destination path must
        // keep the same identity, or a hot-reload swap can never recognize "this is an
        // update to an existing mesh" — every re-cook would otherwise mint a fresh
        // random UUID and look like a brand new, unrelated asset.
        {
            auto identity = AssetCodec::RestoreAssetUUID(*config.VFS, mesh_path.Value(), mesh.MeshUUID);
            if (identity.Failed())
            {
                if (on_error)
                    on_error(context, fmt::format("Failed to read mesh metadata (VFS error {})", static_cast<uint32_t>(identity.Error())));
                Arena.Clear();
                return;
            }
        }

        // Apply per-vertex transform options
        {
            const float scale   = config.Options.UniformScale;
            const bool  axis_z  = config.Options.AxisUpIsZ;
            const bool  flip_uv = config.Options.FlipUVs;
            if (scale != 1.0f || axis_z || flip_uv)
            {
                for (size_t vi = 0; vi < mesh.Vertices.size(); vi += 8)
                {
                    if (scale != 1.0f)
                    {
                        mesh.Vertices[vi + 0] *= scale;
                        mesh.Vertices[vi + 1] *= scale;
                        mesh.Vertices[vi + 2] *= scale;
                    }
                    if (axis_z)
                    {
                        float py              = mesh.Vertices[vi + 1];
                        float pz              = mesh.Vertices[vi + 2];
                        mesh.Vertices[vi + 1] = pz;
                        mesh.Vertices[vi + 2] = -py;
                        float ny              = mesh.Vertices[vi + 4];
                        float nz              = mesh.Vertices[vi + 5];
                        mesh.Vertices[vi + 4] = nz;
                        mesh.Vertices[vi + 5] = -ny;
                    }
                    if (flip_uv)
                        mesh.Vertices[vi + 7] = 1.0f - mesh.Vertices[vi + 7];
                }
            }
        }

        // Optimize each submesh: vertex cache, overdraw, vertex fetch.
        for (uint32_t si = 0; si < mesh.SubMeshes.size(); ++si)
        {
            auto&     sub     = mesh.SubMeshes[si];
            uint32_t* sub_idx = mesh.Indices.data() + sub.IndexOffset;

            // glTF indices are already local to the primitive's vertex slice.
            // Extraction validated both the range and triangle count.
            Importers::OptimizeMeshSubmesh(mesh.Vertices.data() + sub.VertexOffset * 8, sub.VertexCount, sub_idx, sub.IndexCount);
        }

        if (config.Options.ImportMaterials)
        {
            ExtractMaterials(&scratch, asset, gen, materials);

            // Stabilize material UUIDs (#762): re-importing the same source must
            // keep each material's identity, or hot-reload can never recognize it as
            // an update. Keyed by the same (stable, name-derived) destination path
            // SerializeMaterialAssetFile uses. Runs before BuildHierarchy below,
            // which reads MaterialUUID by reference off this same materials array.
            for (size_t m = 0; m < materials.size(); ++m)
            {
                std::string mat_filename = AssetCodec::MaterialOutputFilename(materials[m], m);
                auto        mat_path     = AssetCodec::MaterialOutputPath(materials[m], config, m);
                if (mat_path.Failed())
                {
                    if (on_error)
                        on_error(context, fmt::format("Invalid material output '{}' (VFS error {})", mat_filename, static_cast<uint32_t>(mat_path.Error())));
                    Arena.Clear();
                    return;
                }
                auto identity = AssetCodec::RestoreAssetUUID(*config.VFS, mat_path.Value(), materials[m].MaterialUUID);
                if (identity.Failed())
                {
                    if (on_error)
                        on_error(context, fmt::format("Failed to read material metadata (VFS error {})", static_cast<uint32_t>(identity.Error())));
                    Arena.Clear();
                    return;
                }
            }

            if (config.Options.ImportTextures)
                ExtractTextures(&scratch, asset, gen, textures, materials);
        }
        BuildHierarchy(&scratch, asset, gen, hierarchy, mesh, materials, hierarchy_capacity);

        // Extract texture image bytes to disk and record project-relative paths
        if (config.Options.ImportTextures && config.Options.ImportMaterials)
        {
            auto texture_dir = ZEngine::Core::VFS::VFSPath::Parse(config.OutputTextureFilesPath.c_str()).Value() / config.AssetName.c_str();
            auto create_dir  = config.VFS->CreateDir(texture_dir);
            if (create_dir.Failed() && create_dir.Error() != ZEngine::Core::VFS::VFSError::AlreadyExists)
            {
                if (on_error)
                    on_error(context, "Unable to create the texture output directory");
                Arena.Clear();
                return;
            }

            for (size_t tex_idx = 0; tex_idx < textures.size() && tex_idx < asset.textures.size(); ++tex_idx)
            {
                const auto& fgltf_tex = asset.textures[tex_idx];
                if (!fgltf_tex.imageIndex.has_value())
                    continue;
                const auto&          img    = asset.images[fgltf_tex.imageIndex.value()];
                const uint8_t*       bytes  = nullptr;
                size_t               nbytes = 0;
                const char*          ext    = ".png";

                // Lifetime holder for URI cases — must outlive fwrite below.
                std::vector<uint8_t> uri_file_buf;

                if (const auto* arr = std::get_if<fastgltf::sources::Array>(&img.data))
                {
                    bytes  = reinterpret_cast<const uint8_t*>(arr->bytes.data());
                    nbytes = arr->bytes.size();
                    if (arr->mimeType == fastgltf::MimeType::JPEG)
                        ext = ".jpg";
                }
                else if (const auto* bv_data = std::get_if<fastgltf::sources::ByteView>(&img.data))
                {
                    bytes  = reinterpret_cast<const uint8_t*>(bv_data->bytes.data());
                    nbytes = bv_data->bytes.size();
                    if (bv_data->mimeType == fastgltf::MimeType::JPEG)
                        ext = ".jpg";
                }
                else if (const auto* bv_src = std::get_if<fastgltf::sources::BufferView>(&img.data))
                {
                    const auto view = BufferViewBytes(asset, bv_src->bufferViewIndex);
                    bytes           = view.data();
                    nbytes          = view.size();
                    if (bv_src->mimeType == fastgltf::MimeType::JPEG)
                        ext = ".jpg";
                }
                else if (const auto* uri_src = std::get_if<fastgltf::sources::URI>(&img.data))
                {
                    if (uri_src->uri.isLocalPath())
                    {
                        // External texture file — resolve relative to the source GLTF directory.
                        char resolved[MAX_FILE_PATH_COUNT] = {};
                        auto path_sv                       = uri_src->uri.path();
                        snprintf(resolved, sizeof(resolved), "%s/%.*s", config.InputBaseAssetFilePath.c_str(), static_cast<int>(path_sv.size()), path_sv.data());

                        if (FILE* src_file = fopen(resolved, "rb"))
                        {
                            fseek(src_file, 0, SEEK_END);
                            long file_sz = ftell(src_file);
                            fseek(src_file, 0, SEEK_SET);
                            if (file_sz > 0)
                            {
                                uri_file_buf.resize(static_cast<size_t>(file_sz));
                                if (fread(uri_file_buf.data(), 1, uri_file_buf.size(), src_file) == uri_file_buf.size())
                                {
                                    bytes  = uri_file_buf.data();
                                    nbytes = uri_file_buf.size();
                                }
                                // Derive extension from URI path
                                if (path_sv.size() >= 4)
                                {
                                    auto tail = path_sv.substr(path_sv.size() - 4);
                                    if (tail == ".jpg" || tail == ".JPG")
                                        ext = ".jpg";
                                }
                                if (uri_src->mimeType == fastgltf::MimeType::JPEG)
                                    ext = ".jpg";
                            }
                            fclose(src_file);
                        }
                    }
                    else if (uri_src->uri.isDataUri())
                    {
                        // data:image/<type>;base64,<data>
                        // LoadExternalImages should have decoded this to sources::Array already;
                        // handle defensively for completeness.
                        auto sv    = uri_src->uri.string();
                        auto comma = sv.find(',');
                        if (comma != std::string_view::npos)
                        {
                            auto mime = sv.substr(0, comma);
                            if (mime.find("jpeg") != std::string_view::npos || mime.find("jpg") != std::string_view::npos)
                                ext = ".jpg";

                            auto b64 = sv.substr(comma + 1);
                            if (b64.size() >= 4 && b64.size() % 4 == 0)
                            {
                                auto decoded = fastgltf::base64::decode(b64);
                                uri_file_buf.assign(decoded.begin(), decoded.end());
                                bytes  = uri_file_buf.data();
                                nbytes = uri_file_buf.size();
                            }
                        }
                    }
                    else
                    {
                        ZENGINE_LOG_ASSET_WARN("GltfImporter: tex {} URI scheme not supported (only local paths and data URIs)", tex_idx)
                    }
                }

                if (!bytes || nbytes == 0)
                {
                    if (on_error)
                        on_error(context, fmt::format("Unable to read texture {} image data", tex_idx));
                    Arena.Clear();
                    return;
                }

                // Build output filename — prefer image name, then URI filename, then tex_N
                char stem_buf[256] = {};
                if (!img.name.empty())
                {
                    Helpers::secure_strncpy(stem_buf, sizeof(stem_buf), img.name.data(), img.name.size());
                }
                else if (const auto* uri_src = std::get_if<fastgltf::sources::URI>(&img.data); uri_src && uri_src->uri.isLocalPath())
                {
                    auto path_sv = uri_src->uri.path();
                    auto slash   = path_sv.rfind('/');
                    auto fname   = (slash != std::string_view::npos) ? path_sv.substr(slash + 1) : path_sv;
                    auto dot     = fname.rfind('.');
                    auto name    = (dot != std::string_view::npos) ? fname.substr(0, dot) : fname;
                    Helpers::secure_strncpy(stem_buf, sizeof(stem_buf), name.data(), name.size());
                }
                else
                {
                    Helpers::secure_strcpy(stem_buf, sizeof(stem_buf), "image");
                }

                // Image names are optional/non-unique. The source image index
                // preserves identity while textures sharing an image share a file.
                const auto output_name = fmt::format("{}_{}{}", stem_buf, *fgltf_tex.imageIndex, ext);
                auto       output_path = AssetCodec::MakeOutputPath(texture_dir.CStr(), output_name.c_str());
                auto       write       = output_path.Succeeded() ? Core::VFS::WriteFileAtomically(*config.VFS, output_path.Value(), {bytes, nbytes}) : Core::VFS::VFSResult<void>::Fail(output_path.Error());
                if (write.Succeeded())
                {
                    ZENGINE_LOG_ASSET_INFO("GltfImporter: extracted texture '{}' ({} bytes)", output_path.Value().CStr(), nbytes)
                    textures[tex_idx].Path.init(&scratch, output_path.Value().CStr());
                }
                else
                {
                    if (on_error)
                        on_error(context, fmt::format("Failed to write '{}/{}' (VFS error {})", texture_dir.CStr(), output_name, static_cast<uint32_t>(write.Error())));
                    Arena.Clear();
                    return;
                }
            }

            auto metadata = AssetCodec::SynchronizeTextureMetadata(arena, ArrayView{textures}, ArrayView{materials}, config, "GltfImporter");
            if (metadata.Failed())
            {
                if (on_error)
                    on_error(context, fmt::format("Failed to persist texture metadata (VFS error {})", static_cast<uint32_t>(metadata.Error())));
                Arena.Clear();
                return;
            }
        }

        if (on_progress)
            on_progress(context, 0.7f);

        // Serialize to disk — .zemesh + .zematerial (no .zetextures: paths are inline in material)
        // AssetImporterOutput owns std::strings and must use constructed storage.
        std::vector<AssetImporterOutput> outputs;
        outputs.reserve(1 + materials.size());
        auto append_output = [&](Core::VFS::VFSResult<AssetImporterOutput> result, const char* directory, const std::string& filename) {
            if (result.Failed())
            {
                const auto message = fmt::format("Failed to write '{}/{}' (VFS error {})", directory, filename, static_cast<uint32_t>(result.Error()));
                ZENGINE_CORE_ERROR("[GltfImporter] {}", message)
                if (on_error)
                    on_error(context, message);
                return false;
            }
            outputs.push_back(std::move(result.Value()));
            return true;
        };
        if (config.Options.ImportMaterials)
            for (size_t i = 0; i < materials.size(); ++i)
                if (!append_output(AssetCodec::SerializeMaterialAssetFile(arena, materials[i], config, i), config.OutputMaterialPath.c_str(), fmt::format("{}/{}", config.AssetName.c_str(), AssetCodec::MaterialOutputFilename(materials[i], i))))
                {
                    Arena.Clear();
                    return;
                }

        // Publish the mesh only after all referenced dependencies were written.
        // Preserve the mesh-first completion callback ordering.
        if (!append_output(AssetCodec::SerializeMeshAssetFile(arena, mesh, hierarchy, config), config.OutputAssetsPath.c_str(), config.OutputAssetFile.c_str()))
        {
            Arena.Clear();
            return;
        }
        std::rotate(outputs.begin(), outputs.end() - 1, outputs.end());

        auto* mgr = Managers::AssetManager::Instance();
        if (mgr)
        {
            if (config.Options.ImportTextures && config.Options.ImportMaterials)
                Managers::AssetManager::IngestTextures(std::move(textures));
            if (config.Options.ImportMaterials)
                for (size_t i = 0; i < materials.size(); ++i)
                    Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
        }

        if (on_progress)
            on_progress(context, 1.0f);

        if (on_complete)
            on_complete(context, {outputs.data(), outputs.size()});

        Arena.Clear();
    }
} // namespace ZEngine::Importers

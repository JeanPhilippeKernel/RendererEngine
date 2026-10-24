#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Importers/MeshOptimizer.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <fmt/format.h>
#include <ufbx.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <random>
#include <vector>

using ZEngine::Core::VFS::VFSPath;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Importers;
using namespace uuids;

namespace ZEngine::Importers
{
    // Vertex deduplication key — (position, normal, uv) index triple.
    struct VtxKey
    {
        uint32_t pos = 0, nrm = 0, uv = 0;
        bool     operator==(const VtxKey& o) const
        {
            return pos == o.pos && nrm == o.nrm && uv == o.uv;
        }
    };
    static_assert(sizeof(VtxKey) == 3 * sizeof(uint32_t)); // Byte-hashed, without padding.

    namespace
    {
        template <typename T>
        bool Reserve(Array<T>& array, Core::Memory::ArenaAllocator* arena, size_t count)
        {
            array.m_allocator = arena;
            return array.try_reserve(count);
        }

        // Every geometry buffer is reserved before temporary dedup/triangulation
        // storage. A per-mesh rewind then reclaims that workspace without touching
        // persistent output, so peak scratch depends on the largest mesh, not the sum.
        bool ExtractGeometry(Core::Memory::ArenaAllocator* arena, const ufbx_scene& scene, float scale, bool flip_uvs, bool optimize, AssetMesh& mesh)
        {
            constexpr AssetCodec::MeshAssetReadLimits limits = {};
            if (!std::isfinite(scale) || scale <= 0.f || scene.meshes.count > limits.MaxNodes)
                return false;
            size_t triangles = 0, submeshes = 0;
            for (const auto* source : scene.meshes)
            {
                if (source->num_triangles > limits.MaxIndices / 3 - triangles)
                    return false;
                triangles += source->num_triangles;
                submeshes += source->num_triangles != 0;
            }
            if (!triangles || submeshes > limits.MaxSubMeshes || !Reserve(mesh.Vertices, arena, triangles * 3 * 8) || !Reserve(mesh.Indices, arena, triangles * 3) || !Reserve(mesh.SubMeshes, arena, submeshes))
                return false;
            for (const auto* source : scene.meshes)
            {
                if (!source->num_triangles)
                    continue;
                Core::Memory::ArenaScratch         scratch(arena);
                UnorderedHashMap<VtxKey, uint32_t> vertices         = {};
                Array<uint32_t>                    triangle_indices = {};
                if (!vertices.try_init(scratch.Arena(), source->num_triangles * 3 * 2) || !Reserve(triangle_indices, scratch.Arena(), size_t(source->max_face_triangles) * 3))
                    return false;
                const auto vertex_start = static_cast<uint32_t>(mesh.Vertices.size() / 8);
                const auto index_start  = static_cast<uint32_t>(mesh.Indices.size());
                for (const auto face : source->faces)
                {
                    if (face.num_indices < 3)
                        continue;
                    ufbx_panic panic = {};
                    const auto count = ufbx_catch_triangulate_face(&panic, triangle_indices.data(), triangle_indices.capacity(), source, face);
                    if (panic.did_panic || !count || count > source->max_face_triangles || size_t(count) * 3 > mesh.Indices.capacity() - mesh.Indices.size())
                        return false;
                    for (size_t i = 0; i < size_t(count) * 3; ++i)
                    {
                        const auto corner = triangle_indices.data()[i];
                        if (corner >= source->vertex_position.indices.count || (source->vertex_normal.exists && corner >= source->vertex_normal.indices.count) || (source->vertex_uv.exists && corner >= source->vertex_uv.indices.count))
                            return false;
                        const VtxKey key   = {source->vertex_position.indices.data[corner], source->vertex_normal.exists ? source->vertex_normal.indices.data[corner] : 0, source->vertex_uv.exists ? source->vertex_uv.indices.data[corner] : 0};
                        uint32_t     index = 0;
                        if (const auto* found = vertices.find(key))
                            index = *found;
                        else
                        {
                            if (key.pos >= source->vertex_position.values.count || (source->vertex_normal.exists && key.nrm >= source->vertex_normal.values.count) || (source->vertex_uv.exists && key.uv >= source->vertex_uv.values.count) || mesh.Vertices.size() / 8 >= limits.MaxVertices)
                                return false;
                            index = static_cast<uint32_t>(vertices.size()); // Submesh-local, as required by base-vertex draws.
                            vertices.insert(key, index);
                            const auto  position = source->vertex_position.values.data[key.pos];
                            const auto  normal   = source->vertex_normal.exists ? source->vertex_normal.values.data[key.nrm] : ufbx_vec3{0, 1, 0};
                            const auto  uv       = source->vertex_uv.exists ? source->vertex_uv.values.data[key.uv] : ufbx_vec2{};
                            const float values[] = {float(position.x * scale), float(position.y * scale), float(position.z * scale), float(normal.x), float(normal.y), float(normal.z), float(uv.x), flip_uvs ? float(uv.y) : float(1.0 - uv.y)};
                            if (mesh.Vertices.capacity() - mesh.Vertices.size() < 8)
                                return false;
                            for (const auto value : values)
                            {
                                if (!std::isfinite(value))
                                    return false;
                                mesh.Vertices.push(value);
                            }
                        }
                        mesh.Indices.push(index);
                    }
                }
                AssetSubMesh submesh         = {};
                submesh.VertexOffset         = vertex_start;
                submesh.VertexCount          = static_cast<uint32_t>(vertices.size());
                submesh.IndexOffset          = index_start;
                submesh.IndexCount           = static_cast<uint32_t>(mesh.Indices.size()) - index_start;
                submesh.VertexUnitStreamSize = 8 * sizeof(float);
                submesh.IndexUnitStreamSize  = sizeof(uint32_t);
                submesh.StreamOffset         = submesh.VertexOffset * submesh.VertexUnitStreamSize;
                submesh.IndexStreamOffset    = submesh.IndexOffset * submesh.IndexUnitStreamSize;
                submesh.TotalByteSize        = submesh.VertexCount * submesh.VertexUnitStreamSize + submesh.IndexCount * submesh.IndexUnitStreamSize;
                if (!submesh.VertexCount || submesh.IndexCount != source->num_triangles * 3)
                    return false;
                if (optimize)
                    OptimizeMeshSubmesh(mesh.Vertices.data() + size_t(vertex_start) * 8, submesh.VertexCount, mesh.Indices.data() + index_start, submesh.IndexCount);
                mesh.SubMeshes.push(submesh);
            }
            return true;
        }

        bool CopyString(Core::Memory::ArenaAllocator* arena, String& string, const char* text, size_t length)
        {
            constexpr AssetCodec::MeshAssetReadLimits limits = {};
            if (length > limits.MaxStringBytes || (!text && length))
                return false;
            auto* copy = static_cast<char*>(arena->AllocateNoZero(length + 1, alignof(char)));
            if (!copy || (length && Helpers::secure_memcpy(copy, length + 1, text, length) != Helpers::MEMORY_OP_SUCCESS))
                return false;
            copy[length]       = '\0';
            string.m_allocator = arena;
            string.m_data      = copy;
            string.m_size      = length;
            string.m_capacity  = length + 1;
            return true;
        }

        bool AppendName(Core::Memory::ArenaAllocator* arena, Array<String>& names, const char* text, size_t length, size_t& string_bytes)
        {
            constexpr AssetCodec::MeshAssetReadLimits limits = {};
            if (names.size() == names.capacity() || length >= limits.MaxStringTotalBytes - string_bytes)
                return false;
            String name = {};
            if (!CopyString(arena, name, text, length))
                return false;
            // Exact reservation precedes every String construction; there is no
            // byte relocation of live owning/nontrivial elements.
            new (names.m_data + names.m_size) String(std::move(name));
            ++names.m_size;
            string_bytes += length + 1;
            return true;
        }

        bool ExtractMaterialBindings(Core::Memory::ArenaAllocator* arena, const ufbx_scene& scene, uuid_random_generator& generator, AssetMesh& mesh, Array<AssetMaterial>& materials, Array<AssetTexture>& textures, bool import_materials, bool import_textures)
        {
            if (!import_materials)
                return true;
            if (!Reserve(materials, arena, mesh.SubMeshes.size()) || (import_textures && !Reserve(textures, arena, 3 * mesh.SubMeshes.size())))
                return false;
            size_t submesh = 0;
            for (const auto* source : scene.meshes)
            {
                if (!source->num_triangles)
                    continue;
                auto& sub = mesh.SubMeshes[submesh++];
                if (source->materials.count == 0 || !source->materials.data[0])
                    continue;
                const auto&   source_material = *source->materials.data[0];
                AssetMaterial material        = {};
                material.MaterialUUID         = generator();
                if (!CopyString(arena, material.Name, source_material.name.data, source_material.name.length))
                    return false;
                bool       textures_valid = true;
                const auto texture_id     = [&](const ufbx_material_map& map) -> uuid {
                    if (!import_textures || !map.texture_enabled || !map.texture || !map.texture->filename.length)
                        return {};
                    AssetTexture texture = {};
                    texture.TextureUUID  = generator();
                    if (!CopyString(arena, texture.Path, map.texture->filename.data, map.texture->filename.length))
                    {
                        textures_valid = false;
                        return {};
                    }
                    const auto id = texture.TextureUUID;
                    new (textures.m_data + textures.m_size) AssetTexture(std::move(texture));
                    ++textures.m_size;
                    return id;
                };
                material.AlbedoTexUUID     = texture_id(source_material.pbr.base_color);
                material.NormalTexUUID     = texture_id(source_material.pbr.normal_map);
                material.SpecularTexUUID   = texture_id(source_material.pbr.metalness);
                const auto color           = source_material.pbr.base_color.value_vec4;
                material.AlbedoColor[0]    = float(color.x);
                material.AlbedoColor[1]    = float(color.y);
                material.AlbedoColor[2]    = float(color.z);
                material.AlbedoColor[3]    = float(color.w);
                material.Factors[1]        = float(source_material.pbr.metalness.value_real);
                material.RoughnessColor[0] = float(source_material.pbr.roughness.value_real);
                for (const float value : {material.AlbedoColor[0], material.AlbedoColor[1], material.AlbedoColor[2], material.AlbedoColor[3], material.Factors[1], material.RoughnessColor[0]})
                    if (!std::isfinite(value))
                        return false;
                if (!textures_valid)
                    return false;
                sub.MaterialUUID = material.MaterialUUID;
                new (materials.m_data + materials.m_size) AssetMaterial(std::move(material));
                ++materials.m_size;
            }
            return true;
        }

        bool ConvertMatrix(const ufbx_matrix& source, float scale, Mat4f& output)
        {
            output = Identity<Mat4f>();
            for (uint32_t column = 0; column < 4; ++column)
                for (uint32_t row = 0; row < 3; ++row)
                {
                    const auto value = float(source.v[column * 3 + row] * (column == 3 ? scale : 1.f));
                    if (!std::isfinite(value))
                        return false;
                    output(row, column) = value;
                }
            return true;
        }

        bool BuildHierarchy(Core::Memory::ArenaAllocator* arena, const ufbx_scene& source, float scale, uuid_random_generator& generator, const AssetMesh& mesh, const Array<AssetMaterial>& materials, AssetNodeHierarchy& hierarchy)
        {
            constexpr AssetCodec::MeshAssetReadLimits limits   = {};
            size_t                                    capacity = source.nodes.count;
            for (const auto* node : source.nodes)
                capacity += node->mesh && node->mesh->num_triangles;
            if (!capacity || capacity > limits.MaxNodes || materials.size() > limits.MaxNames)
                return false;
            hierarchy.MeshUUID          = mesh.MeshUUID;
            hierarchy.NodeHierarchyUUID = generator();
            if (hierarchy.NodeHierarchyUUID.is_nil() || hierarchy.NodeHierarchyUUID == mesh.MeshUUID || !Reserve(hierarchy.Hierarchies, arena, capacity) || !Reserve(hierarchy.LocalTransforms, arena, capacity) || !Reserve(hierarchy.GlobalTransforms, arena, capacity) || !Reserve(hierarchy.Names, arena, capacity) || !Reserve(hierarchy.MaterialNames, arena, materials.size()) || !hierarchy.NodeNames.try_init(arena, 2 * capacity) || !hierarchy.NodeMeshes.try_init(arena, 2 * source.nodes.count) || !hierarchy.NodeMaterials.try_init(arena, 2 * source.nodes.count))
                return false;
            UnorderedHashMap<uuid, uint32_t> material_indices = {};
            Array<uint32_t>                  mesh_indices     = {};
            if (!material_indices.try_init(arena, 2 * materials.size()) || !Reserve(mesh_indices, arena, source.meshes.count))
                return false;
            uint32_t submesh      = 0;
            size_t   string_bytes = 0;
            for (const auto* source_mesh : source.meshes)
                mesh_indices.push(source_mesh->num_triangles ? submesh++ : UFBX_NO_INDEX);
            for (const auto& material : materials)
            {
                const auto index = static_cast<uint32_t>(hierarchy.MaterialNames.size());
                if (!AppendName(arena, hierarchy.MaterialNames, material.Name.c_str(), material.Name.size(), string_bytes))
                    return false;
                material_indices.insert(material.MaterialUUID, index);
            }
            struct PendingNode
            {
                const ufbx_node* Source = nullptr;
                int              Parent = -1;
                int              Depth  = 0;
            };
            Array<PendingNode> pending = {};
            if (!Reserve(pending, arena, source.nodes.count))
                return false;
            for (size_t i = source.nodes.count; i > 0; --i)
                if (!source.nodes.data[i - 1]->parent)
                    pending.push({source.nodes.data[i - 1], -1, 0});
            while (!pending.empty())
            {
                const auto next = pending.back();
                pending.pop();
                const auto& node = *next.Source;
                const auto  id   = AddNode(hierarchy, next.Parent, next.Depth);
                if (id < 0 || !ConvertMatrix(node.node_to_parent, scale, hierarchy.LocalTransforms[id]) || !ConvertMatrix(node.node_to_world, scale, hierarchy.GlobalTransforms[id]))
                    return false;
                hierarchy.NodeNames.insert(id, static_cast<uint32_t>(hierarchy.Names.size()));
                if (!AppendName(arena, hierarchy.Names, node.name.length ? node.name.data : "<node>", node.name.length ? node.name.length : 6, string_bytes))
                    return false;
                if (node.mesh && node.mesh->num_triangles)
                {
                    if (node.mesh->typed_id >= mesh_indices.size() || mesh_indices[node.mesh->typed_id] >= mesh.SubMeshes.size())
                        return false;
                    const auto mesh_index = mesh_indices[node.mesh->typed_id];
                    const auto child      = AddNode(hierarchy, id, next.Depth + 1);
                    if (child < 0)
                        return false;
                    hierarchy.NodeMeshes.insert(child, mesh_index);
                    hierarchy.NodeNames.insert(child, static_cast<uint32_t>(hierarchy.Names.size()));
                    if (!AppendName(arena, hierarchy.Names, node.mesh->name.length ? node.mesh->name.data : "<mesh>", node.mesh->name.length ? node.mesh->name.length : 6, string_bytes) || !ConvertMatrix(node.geometry_to_node, scale, hierarchy.LocalTransforms[child]) || !ConvertMatrix(node.geometry_to_world, scale, hierarchy.GlobalTransforms[child]))
                        return false;
                    if (const auto* material = material_indices.find(mesh.SubMeshes[mesh_index].MaterialUUID))
                        hierarchy.NodeMaterials.insert(child, *material);
                }
                for (size_t i = node.children.count; i > 0; --i)
                    if (!pending.try_push({node.children.data[i - 1], id, next.Depth + 1}))
                        return false;
            }
            return hierarchy.Hierarchies.size() == capacity;
        }
    } // namespace

    void FbxImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(512), &Arena, "ImportPipeline/FbxImporter");
    }

    bool FbxImporter::CanImport(const char* extension) const
    {
        return extension && Helpers::secure_strcmp(extension, "fbx") == 0;
    }

    Core::VFS::VFSResult<void> FbxImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        std::lock_guard import_lock(m_import_mutex);
        if (!Arena.m_memory)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
        char        native[MAX_FILE_PATH_COUNT] = {};
        const char* working_space               = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
        if (working_space && working_space[0] != '\0')
            path.ResolveNative(working_space, native, sizeof(native));
        else
            path.ToNative(native, sizeof(native));

        AssetCodec::ImportConfiguration config = {};
        config.VFS                             = &ctx;

        AssetMesh             mesh             = {};
        AssetNodeHierarchy    hier             = {};
        Array<AssetMaterial>  materials        = {};
        Array<AssetTexture>   textures         = {};

        auto                  scratch          = ZGetScratch(&Arena);

        std::random_device    rd;
        std::mt19937          gen_mt(rd());
        uuid_random_generator gen(&gen_mt);

        ufbx_load_opts        opts         = {};
        opts.target_axes                   = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters            = 1.0f;
        opts.generate_missing_normals      = true;
        opts.index_error_handling          = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
        opts.inherit_mode_handling         = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        opts.temp_allocator.memory_limit   = ZMega(256);
        opts.result_allocator.memory_limit = ZMega(256);

        ufbx_error  error                  = {};
        ufbx_scene* scene                  = ufbx_load_file(native, &opts, &error);
        if (!scene)
        {
            ZENGINE_CORE_ERROR("[FbxImporter] Failed to load '{}': {}", native, error.description.data)
            ZReleaseScratch(scratch);
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        mesh.MeshUUID = meta.AssetUUID;
        if (mesh.MeshUUID.is_nil() || !ExtractGeometry(scratch.Arena, *scene, 1.f, false, false, mesh))
        {
            ufbx_free_scene(scene);
            ZReleaseScratch(scratch);
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        if (!ExtractMaterialBindings(scratch.Arena, *scene, gen, mesh, materials, textures, true, true))
        {
            ufbx_free_scene(scene);
            ZReleaseScratch(scratch);
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }

        if (!BuildHierarchy(scratch.Arena, *scene, 1.f, gen, mesh, materials, hier))
        {
            ufbx_free_scene(scene);
            ZReleaseScratch(scratch);
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        ufbx_free_scene(scene);

        if (Managers::AssetManager::Instance())
        {
            Managers::AssetManager::IngestTextures(std::move(textures));
            for (size_t i = 0; i < materials.size(); ++i)
                Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hier));
        }

        ZReleaseScratch(scratch);
        return Core::VFS::VFSResult<void>::Ok();
    }

    void FbxImporter::ImportFile(const char* filename, const AssetCodec::ImportConfiguration& cfg, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log)
    {
        std::lock_guard import_lock(m_import_mutex);
        auto            mesh_path = AssetCodec::ValidateImportConfiguration(cfg);
        if (mesh_path.Failed() || !filename || !filename[0])
        {
            if (on_error)
                on_error(context, fmt::format("Invalid import configuration (VFS error {})", static_cast<uint32_t>(mesh_path.Failed() ? mesh_path.Error() : Core::VFS::VFSError::InvalidPath)));
            return;
        }
        // The caller's arena is sized only for a few short path strings (#760) — route
        // everything through this importer's own, generously-sized private Arena instead.
        if (!Arena.m_memory)
        {
            if (on_error)
                on_error(context, "FBX importer workspace is not initialized.");
            return;
        }
        Arena.Clear();
        Core::Memory::ArenaScratch import_memory(&Arena);
        arena                                  = import_memory.Arena();
        AssetCodec::ImportConfiguration config = {};
        if (!CopyString(arena, config.OutputWorkingSpacePath, cfg.OutputWorkingSpacePath.c_str(), cfg.OutputWorkingSpacePath.size()) || !CopyString(arena, config.OutputTextureFilesPath, cfg.OutputTextureFilesPath.c_str(), cfg.OutputTextureFilesPath.size()) || !CopyString(arena, config.OutputAssetsPath, cfg.OutputAssetsPath.c_str(), cfg.OutputAssetsPath.size()) || !CopyString(arena, config.OutputMaterialPath, cfg.OutputMaterialPath.c_str(), cfg.OutputMaterialPath.size()) || !CopyString(arena, config.AssetName, cfg.AssetName.c_str(), cfg.AssetName.size()) ||
            !CopyString(arena, config.OutputAssetFile, cfg.OutputAssetFile.c_str(), cfg.OutputAssetFile.size()) || !CopyString(arena, config.InputBaseAssetFilePath, cfg.InputBaseAssetFilePath.c_str(), cfg.InputBaseAssetFilePath.size()))
        {
            if (on_error)
                on_error(context, "FBX output paths exceed the bounded import workspace.");
            return;
        }
        config.VFS                         = cfg.VFS;
        config.ArtifactContext             = cfg.ArtifactContext;
        config.OnArtifactPublished         = cfg.OnArtifactPublished;
        config.Options                     = cfg.Options;

        ufbx_load_opts opts                = {};
        opts.target_axes                   = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters            = 1.0f;
        opts.generate_missing_normals      = (config.Options.NormalsMode > 0);
        opts.index_error_handling          = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
        opts.inherit_mode_handling         = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        opts.temp_allocator.memory_limit   = ZMega(256);
        opts.result_allocator.memory_limit = ZMega(256);

        ufbx_error  error                  = {};
        ufbx_scene* scene                  = ufbx_load_file(filename, &opts, &error);

        if (!scene)
        {
            if (on_error)
                on_error(context, error.description.data);
            return;
        }

        if (on_progress)
            on_progress(context, 0.2f);

        std::random_device    rd;
        std::mt19937          gen_mt(rd());
        uuid_random_generator gen(&gen_mt);

        AssetMesh             mesh      = {};
        AssetNodeHierarchy    hier      = {};
        Array<AssetMaterial>  materials = {};
        Array<AssetTexture>   textures  = {};

        mesh.MeshUUID                   = gen();

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
                ufbx_free_scene(scene);
                return;
            }
        }

        if (!ExtractGeometry(arena, *scene, config.Options.UniformScale, config.Options.FlipUVs, true, mesh))
        {
            if (on_error)
                on_error(context, "FBX geometry is invalid or exceeds the bounded import workspace.");
            ufbx_free_scene(scene);
            return;
        }
        if (!ExtractMaterialBindings(arena, *scene, gen, mesh, materials, textures, config.Options.ImportMaterials, config.Options.ImportTextures))
        {
            if (on_error)
                on_error(context, "FBX material bindings are invalid or exceed the bounded import workspace.");
            ufbx_free_scene(scene);
            return;
        }

        if (on_progress)
            on_progress(context, 0.5f);

        // Stabilize material UUIDs (#762): re-importing the same source must keep
        // each material's identity, or hot-reload can never recognize it as an
        // update. Keyed by the same (stable, name-derived) destination path
        // SerializeMaterialAssetFile uses. Unlike Assimp/Gltf, the loop above
        // already copied each material's original UUID into its SubMesh by value
        // (not by reference), so matching submeshes need an explicit patch too.
        if (config.Options.ImportMaterials)
        {
            for (size_t m = 0; m < materials.size(); ++m)
            {
                std::string mat_filename = AssetCodec::MaterialOutputFilename(materials[m], m);
                auto        mat_path     = AssetCodec::MaterialOutputPath(materials[m], config, m);
                if (mat_path.Failed())
                {
                    if (on_error)
                        on_error(context, fmt::format("Invalid material output '{}' (VFS error {})", mat_filename, static_cast<uint32_t>(mat_path.Error())));
                    ufbx_free_scene(scene);
                    return;
                }
                const auto old_uuid = materials[m].MaterialUUID;
                auto       identity = AssetCodec::RestoreAssetUUID(*config.VFS, mat_path.Value(), materials[m].MaterialUUID);
                if (identity.Failed())
                {
                    if (on_error)
                        on_error(context, fmt::format("Failed to read material metadata (VFS error {})", static_cast<uint32_t>(identity.Error())));
                    ufbx_free_scene(scene);
                    return;
                }
                if (old_uuid != materials[m].MaterialUUID)
                {
                    for (size_t s = 0; s < mesh.SubMeshes.size(); ++s)
                        if (mesh.SubMeshes[s].MaterialUUID == old_uuid)
                            mesh.SubMeshes[s].MaterialUUID = materials[m].MaterialUUID;
                }
            }
        }

        if (config.Options.ImportMaterials && config.Options.ImportTextures)
        {
            auto copy = AssetCodec::CopyTextureFiles(ArrayView{textures}, config);
            if (copy.Failed())
            {
                if (on_error)
                    on_error(context, fmt::format("Failed to copy texture files (VFS error {})", static_cast<uint32_t>(copy.Error())));
                ufbx_free_scene(scene);
                return;
            }

            auto metadata = AssetCodec::SynchronizeTextureMetadata(arena, ArrayView{textures}, ArrayView{materials}, config, "FbxImporter");
            if (metadata.Failed())
            {
                if (on_error)
                    on_error(context, fmt::format("Failed to persist texture metadata (VFS error {})", static_cast<uint32_t>(metadata.Error())));
                ufbx_free_scene(scene);
                return;
            }
        }

        if (on_progress)
            on_progress(context, 0.8f);

        if (!BuildHierarchy(arena, *scene, config.Options.UniformScale, gen, mesh, materials, hier))
        {
            if (on_error)
                on_error(context, "FBX hierarchy is invalid or exceeds the bounded import workspace.");
            ufbx_free_scene(scene);
            return;
        }
        ufbx_free_scene(scene);

        std::vector<AssetImporterOutput> outputs;
        outputs.reserve(1 + materials.size());
        auto append_output = [&](Core::VFS::VFSResult<AssetImporterOutput> result, const char* directory, const std::string& filename) {
            if (result.Failed())
            {
                const auto message = fmt::format("Failed to write '{}/{}' (VFS error {})", directory, filename, static_cast<uint32_t>(result.Error()));
                ZENGINE_CORE_ERROR("[FbxImporter] {}", message)
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
                    return;
                }

        // Dependencies first; publish the mesh last while keeping callback order.
        if (!append_output(AssetCodec::SerializeMeshAssetFile(arena, mesh, hier, config), config.OutputAssetsPath.c_str(), config.OutputAssetFile.c_str()))
        {
            return;
        }
        std::rotate(outputs.begin(), outputs.end() - 1, outputs.end());

        // ImportFile cooks artifacts only. The editor validates its originating
        // scene before binding them on the main thread. Runtime Import() retains
        // the separate coordinator-owned ingest path.

        if (on_progress)
            on_progress(context, 1.0f);
        if (on_complete)
            on_complete(context, {outputs.data(), outputs.size()});
    }

} // namespace ZEngine::Importers

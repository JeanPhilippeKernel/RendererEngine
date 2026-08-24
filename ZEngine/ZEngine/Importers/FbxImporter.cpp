#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ufbx.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

using ZEngine::Core::VFS::VFSPath;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Importers;
using namespace uuids;

namespace fs = std::filesystem;

namespace ZEngine::Importers
{
    // Vertex deduplication key — (position, normal, uv) index triple.
    struct VtxKey
    {
        uint32_t pos, nrm, uv;
        bool     operator==(const VtxKey& o) const
        {
            return pos == o.pos && nrm == o.nrm && uv == o.uv;
        }
    };
    struct VtxKeyHash
    {
        size_t operator()(const VtxKey& k) const
        {
            size_t h  = (size_t) k.pos * 2654435761u;
            h        ^= (size_t) k.nrm * 2246822519u;
            h        ^= (size_t) k.uv * 3266489917u;
            return h;
        }
    };

    void FbxImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(256), &Arena);
    }

    bool FbxImporter::CanImport(const char* extension) const
    {
        return extension && Helpers::secure_strcmp(extension, "fbx") == 0;
    }

    Core::VFS::VFSResult<void> FbxImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        char        native[MAX_FILE_PATH_COUNT] = {};
        const char* working_space               = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
        if (working_space && working_space[0] != '\0')
            path.ResolveNative(working_space, native, sizeof(native));
        else
            path.ToNative(native, sizeof(native));

        AssetCodec::ImportConfiguration config = {};
        config.VFS                             = &ctx;

        AssetMesh                    mesh      = {};
        AssetNodeHierarchy           hier      = {};
        Array<AssetMaterial>         materials = {};
        Array<AssetTexture>          textures  = {};

        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(64), &scratch);

        std::random_device    rd;
        std::mt19937          gen_mt(rd());
        uuid_random_generator gen(&gen_mt);

        ufbx_load_opts        opts    = {};
        opts.target_axes              = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters       = 1.0f;
        opts.generate_missing_normals = true;

        ufbx_error  error;
        ufbx_scene* scene = ufbx_load_file(native, &opts, &error);
        if (!scene)
        {
            ZENGINE_CORE_ERROR("[FbxImporter] Failed to load '{}': {}", native, error.description.data)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        mesh.MeshUUID = meta.AssetUUID;
        mesh.Vertices.init(&scratch, 1024);
        mesh.Indices.init(&scratch, 1024);
        mesh.SubMeshes.init(&scratch, (uint32_t) scene->meshes.count);
        materials.init(&scratch, 64);
        textures.init(&scratch, 256);

        for (size_t mi = 0; mi < scene->meshes.count; ++mi)
        {
            ufbx_mesh*                                       fbx_mesh = scene->meshes.data[mi];
            std::unordered_map<VtxKey, uint32_t, VtxKeyHash> vtx_map;
            vtx_map.reserve(fbx_mesh->num_triangles * 3);

            uint32_t sub_vtx_start = static_cast<uint32_t>(mesh.Vertices.size() / 8);
            uint32_t sub_idx_start = static_cast<uint32_t>(mesh.Indices.size());

            uint32_t tri_buf[512];
            for (size_t fi = 0; fi < fbx_mesh->faces.count; ++fi)
            {
                ufbx_face face     = fbx_mesh->faces.data[fi];
                uint32_t  num_tris = ufbx_triangulate_face(tri_buf, 512, fbx_mesh, face);

                for (uint32_t ti = 0; ti < num_tris; ++ti)
                {
                    for (uint32_t vi = 0; vi < 3; ++vi)
                    {
                        uint32_t fv = tri_buf[ti * 3 + vi];

                        uint32_t pi = (uint32_t) fbx_mesh->vertex_position.indices.data[fv];
                        uint32_t ni = fbx_mesh->vertex_normal.exists ? (uint32_t) fbx_mesh->vertex_normal.indices.data[fv] : 0;
                        uint32_t ui = fbx_mesh->vertex_uv.exists ? (uint32_t) fbx_mesh->vertex_uv.indices.data[fv] : 0;

                        VtxKey   key{pi, ni, ui};
                        auto     it = vtx_map.find(key);

                        uint32_t flat_ix;
                        if (it == vtx_map.end())
                        {
                            flat_ix = sub_vtx_start + static_cast<uint32_t>(vtx_map.size());
                            vtx_map.emplace(key, flat_ix);

                            ufbx_vec3 pos = fbx_mesh->vertex_position.values.data[pi];
                            ufbx_vec3 nrm = fbx_mesh->vertex_normal.exists ? fbx_mesh->vertex_normal.values.data[ni] : ufbx_vec3{0, 1, 0};
                            ufbx_vec2 uv  = fbx_mesh->vertex_uv.exists ? fbx_mesh->vertex_uv.values.data[ui] : ufbx_vec2{0, 0};

                            mesh.Vertices.push(static_cast<float>(pos.x));
                            mesh.Vertices.push(static_cast<float>(pos.y));
                            mesh.Vertices.push(static_cast<float>(pos.z));
                            mesh.Vertices.push(static_cast<float>(nrm.x));
                            mesh.Vertices.push(static_cast<float>(nrm.y));
                            mesh.Vertices.push(static_cast<float>(nrm.z));
                            mesh.Vertices.push(static_cast<float>(uv.x));
                            mesh.Vertices.push(static_cast<float>(1.0 - uv.y)); // flip V — FBX convention
                        }
                        else
                        {
                            flat_ix = it->second;
                        }
                        mesh.Indices.push(flat_ix);
                    }
                }
            }

            AssetSubMesh sub         = {};
            sub.VertexOffset         = sub_vtx_start;
            sub.VertexCount          = static_cast<uint32_t>(vtx_map.size());
            sub.IndexOffset          = sub_idx_start;
            sub.IndexCount           = static_cast<uint32_t>(mesh.Indices.size()) - sub_idx_start;
            sub.VertexUnitStreamSize = 8 * sizeof(float);
            sub.IndexUnitStreamSize  = sizeof(uint32_t);
            sub.TotalByteSize        = sub.VertexCount * sub.VertexUnitStreamSize;

            if (fbx_mesh->materials.count > 0 && fbx_mesh->materials.data[0])
            {
                ufbx_material* mat   = fbx_mesh->materials.data[0];
                AssetMaterial  a_mat = {};
                a_mat.MaterialUUID   = gen();
                a_mat.Name.init(&scratch, mat->name.data);
                sub.MaterialUUID = a_mat.MaterialUUID;

                auto tex_uuid    = [&](ufbx_material_map& map) -> uuids::uuid {
                    if (map.texture_enabled && map.texture && map.texture->filename.length > 0)
                    {
                        uuids::uuid  id = gen();
                        AssetTexture t  = {};
                        t.TextureUUID   = id;
                        t.Path.init(&scratch, map.texture->filename.data);
                        textures.push(t);
                        return id;
                    }
                    return {};
                };

                a_mat.AlbedoTexUUID     = tex_uuid(mat->pbr.base_color);
                a_mat.NormalTexUUID     = tex_uuid(mat->pbr.normal_map);
                a_mat.SpecularTexUUID   = tex_uuid(mat->pbr.metalness);

                auto col                = mat->pbr.base_color.value_vec4;
                a_mat.AlbedoColor[0]    = static_cast<float>(col.x);
                a_mat.AlbedoColor[1]    = static_cast<float>(col.y);
                a_mat.AlbedoColor[2]    = static_cast<float>(col.z);
                a_mat.AlbedoColor[3]    = static_cast<float>(col.w);
                a_mat.Factors[1]        = static_cast<float>(mat->pbr.metalness.value_real);
                a_mat.RoughnessColor[0] = static_cast<float>(mat->pbr.roughness.value_real);

                materials.push(a_mat);
            }

            mesh.SubMeshes.push(sub);
        }

        ufbx_free_scene(scene);

        if (Managers::AssetManager::Instance())
        {
            Managers::AssetManager::IngestTextures(std::move(textures));
            for (size_t i = 0; i < materials.size(); ++i)
                Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hier));
        }

        Arena.Clear();
        return Core::VFS::VFSResult<void>::Ok();
    }

    void FbxImporter::ImportFile(const char* filename, const AssetCodec::ImportConfiguration& cfg, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log)
    {
        AssetCodec::ImportConfiguration config = {};
        config.OutputWorkingSpacePath.init(arena, cfg.OutputWorkingSpacePath.c_str());
        config.OutputTextureFilesPath.init(arena, cfg.OutputTextureFilesPath.c_str());
        config.OutputAssetsPath.init(arena, cfg.OutputAssetsPath.c_str());
        config.OutputMaterialPath.init(arena, cfg.OutputMaterialPath.c_str());
        config.AssetName.init(arena, cfg.AssetName.c_str());
        config.OutputAssetFile.init(arena, cfg.OutputAssetFile.c_str());
        config.InputBaseAssetFilePath.init(arena, cfg.InputBaseAssetFilePath.c_str());
        config.VFS                    = cfg.VFS;
        config.Options                = cfg.Options;

        ufbx_load_opts opts           = {};
        opts.target_axes              = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters       = 1.0f;
        opts.generate_missing_normals = (config.Options.NormalsMode > 0);

        ufbx_error  error;
        ufbx_scene* scene = ufbx_load_file(filename, &opts, &error);

        if (!scene)
        {
            if (on_error)
                on_error(context, error.description.data);
            return;
        }

        if (on_progress)
            on_progress(context, 0.2f);

        std::random_device           rd;
        std::mt19937                 gen_mt(rd());
        uuid_random_generator        gen(&gen_mt);

        // Use FbxImporter::Arena for mesh data — large scratch for vertex/index arrays.
        // Only the small config strings above use the caller's arena.
        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(192), &scratch);

        AssetMesh            mesh      = {};
        AssetNodeHierarchy   hier      = {};
        Array<AssetMaterial> materials = {};
        Array<AssetTexture>  textures  = {};

        mesh.MeshUUID                  = gen();
        mesh.Vertices.init(&scratch, 4096);
        mesh.Indices.init(&scratch, 4096);
        mesh.SubMeshes.init(&scratch, (uint32_t) scene->meshes.count);
        materials.init(&scratch, 64);
        textures.init(&scratch, 256);

        const float scale = config.Options.UniformScale;

        for (size_t mi = 0; mi < scene->meshes.count; ++mi)
        {
            ufbx_mesh*                                       fbx_mesh = scene->meshes.data[mi];
            std::unordered_map<VtxKey, uint32_t, VtxKeyHash> vtx_map;
            vtx_map.reserve(fbx_mesh->num_triangles * 2);

            uint32_t sub_vtx_start = static_cast<uint32_t>(mesh.Vertices.size() / 8);
            uint32_t sub_idx_start = static_cast<uint32_t>(mesh.Indices.size());

            uint32_t tri_buf[512];
            for (size_t fi = 0; fi < fbx_mesh->faces.count; ++fi)
            {
                ufbx_face face     = fbx_mesh->faces.data[fi];
                uint32_t  num_tris = ufbx_triangulate_face(tri_buf, 512, fbx_mesh, face);

                for (uint32_t ti = 0; ti < num_tris; ++ti)
                {
                    for (uint32_t vi = 0; vi < 3; ++vi)
                    {
                        uint32_t fv = tri_buf[ti * 3 + vi];
                        uint32_t pi = (uint32_t) fbx_mesh->vertex_position.indices.data[fv];
                        uint32_t ni = fbx_mesh->vertex_normal.exists ? (uint32_t) fbx_mesh->vertex_normal.indices.data[fv] : 0;
                        uint32_t ui = fbx_mesh->vertex_uv.exists ? (uint32_t) fbx_mesh->vertex_uv.indices.data[fv] : 0;

                        VtxKey   key{pi, ni, ui};
                        auto     it = vtx_map.find(key);
                        uint32_t flat_ix;

                        if (it == vtx_map.end())
                        {
                            flat_ix = sub_vtx_start + static_cast<uint32_t>(vtx_map.size());
                            vtx_map.emplace(key, flat_ix);

                            ufbx_vec3 pos    = fbx_mesh->vertex_position.values.data[pi];
                            ufbx_vec3 nrm    = fbx_mesh->vertex_normal.exists ? fbx_mesh->vertex_normal.values.data[ni] : ufbx_vec3{0, 1, 0};
                            ufbx_vec2 uv     = fbx_mesh->vertex_uv.exists ? fbx_mesh->vertex_uv.values.data[ui] : ufbx_vec2{0, 0};

                            float     flip_v = config.Options.FlipUVs ? (float) uv.y : 1.0f - (float) uv.y;

                            mesh.Vertices.push(static_cast<float>(pos.x) * scale);
                            mesh.Vertices.push(static_cast<float>(pos.y) * scale);
                            mesh.Vertices.push(static_cast<float>(pos.z) * scale);
                            mesh.Vertices.push(static_cast<float>(nrm.x));
                            mesh.Vertices.push(static_cast<float>(nrm.y));
                            mesh.Vertices.push(static_cast<float>(nrm.z));
                            mesh.Vertices.push(static_cast<float>(uv.x));
                            mesh.Vertices.push(flip_v);
                        }
                        else
                        {
                            flat_ix = it->second;
                        }
                        mesh.Indices.push(flat_ix);
                    }
                }
            }

            AssetSubMesh sub         = {};
            sub.VertexOffset         = sub_vtx_start;
            sub.VertexCount          = static_cast<uint32_t>(vtx_map.size());
            sub.IndexOffset          = sub_idx_start;
            sub.IndexCount           = static_cast<uint32_t>(mesh.Indices.size()) - sub_idx_start;
            sub.VertexUnitStreamSize = 8 * sizeof(float);
            sub.IndexUnitStreamSize  = sizeof(uint32_t);
            sub.TotalByteSize        = sub.VertexCount * sub.VertexUnitStreamSize;

            if (config.Options.ImportMaterials && fbx_mesh->materials.count > 0 && fbx_mesh->materials.data[0])
            {
                ufbx_material* mat   = fbx_mesh->materials.data[0];
                AssetMaterial  a_mat = {};
                a_mat.MaterialUUID   = gen();
                a_mat.Name.init(&scratch, mat->name.data);
                sub.MaterialUUID = a_mat.MaterialUUID;

                auto push_tex    = [&](ufbx_material_map& map) -> uuids::uuid {
                    if (!config.Options.ImportTextures)
                        return {};
                    if (map.texture_enabled && map.texture && map.texture->filename.length > 0)
                    {
                        uuids::uuid  id = gen();
                        AssetTexture t  = {};
                        t.TextureUUID   = id;
                        t.Path.init(&scratch, map.texture->filename.data);
                        textures.push(t);
                        return id;
                    }
                    return {};
                };

                a_mat.AlbedoTexUUID     = push_tex(mat->pbr.base_color);
                a_mat.NormalTexUUID     = push_tex(mat->pbr.normal_map);
                a_mat.SpecularTexUUID   = push_tex(mat->pbr.metalness);

                auto col                = mat->pbr.base_color.value_vec4;
                a_mat.AlbedoColor[0]    = static_cast<float>(col.x);
                a_mat.AlbedoColor[1]    = static_cast<float>(col.y);
                a_mat.AlbedoColor[2]    = static_cast<float>(col.z);
                a_mat.AlbedoColor[3]    = static_cast<float>(col.w);
                a_mat.Factors[1]        = static_cast<float>(mat->pbr.metalness.value_real);
                a_mat.RoughnessColor[0] = static_cast<float>(mat->pbr.roughness.value_real);

                materials.push(a_mat);
            }

            mesh.SubMeshes.push(sub);
        }

        if (on_progress)
            on_progress(context, 0.5f);

        if (config.Options.ImportMaterials && config.Options.ImportTextures)
            CopyTextureFiles(arena, textures, config);

        if (on_progress)
            on_progress(context, 0.8f);

        ufbx_free_scene(scene);

        hier.MeshUUID          = mesh.MeshUUID;
        hier.NodeHierarchyUUID = mesh.MeshUUID;
        hier.LocalTransforms.init(arena, 1);
        hier.GlobalTransforms.init(arena, 1);
        hier.Hierarchies.init(arena, 1);
        hier.Names.init(arena, 1);
        hier.MaterialNames.init(arena, 1);
        hier.NodeNames.init(&scratch, 64);
        hier.NodeMeshes.init(&scratch, 1);
        hier.NodeMaterials.init(&scratch, 1);
        hier.LocalTransforms.push(Identity<Mat4f>());
        hier.GlobalTransforms.push(Identity<Mat4f>());

        Array<AssetImporterOutput> outputs = {};
        outputs.init(&scratch, 16);
        outputs.push(AssetCodec::SerializeMeshAssetFile(&scratch, mesh, hier, config));
        if (config.Options.ImportMaterials)
            for (size_t i = 0; i < materials.size(); ++i)
                outputs.push(AssetCodec::SerializeMaterialAssetFile(&scratch, materials[i], config));

        auto* mgr = Managers::AssetManager::Instance();
        if (mgr)
        {
            if (config.Options.ImportTextures && config.Options.ImportMaterials)
                Managers::AssetManager::IngestTextures(std::move(textures));
            if (config.Options.ImportMaterials)
                for (size_t i = 0; i < materials.size(); ++i)
                    Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hier));
        }

        if (on_progress)
            on_progress(context, 1.0f);
        if (on_complete)
            on_complete(context, ArrayView{outputs});

        Arena.Clear();
    }

    void FbxImporter::CopyTextureFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::Array<AssetTexture>& textures, const AssetCodec::ImportConfiguration& config)
    {
        if (textures.empty())
            return;

        char dst_dir_buf[MAX_FILE_PATH_COUNT] = {};
        (VFSPath::Parse(config.OutputTextureFilesPath.c_str()).Value() / config.AssetName.c_str()).ResolveNative(config.OutputWorkingSpacePath.c_str(), dst_dir_buf, sizeof(dst_dir_buf));
        if (config.VFS)
            config.VFS->CreateDir(VFSPath::Parse(config.OutputTextureFilesPath.c_str()).Value() / config.AssetName.c_str());

        for (auto& tex : textures)
        {
            if (tex.Path.empty())
                continue;

            fs::path      tex_path(tex.Path.c_str());
            fs::path      src = tex_path.is_absolute() ? tex_path : fs::path(config.InputBaseAssetFilePath.c_str()) / tex_path;
            fs::path      dst = fs::path(dst_dir_buf) / tex_path.filename();

            std::ifstream in(src, std::ios::binary);
            if (!in.is_open())
            {
                ZENGINE_CORE_WARN("[FbxImporter] Texture not found: {}", src.string())
                continue;
            }
            std::ofstream out(dst, std::ios::binary);
            out << in.rdbuf();

            auto new_path = std::string((VFSPath::Parse(config.OutputTextureFilesPath.c_str()).Value() / config.AssetName.c_str() / tex_path.filename().string().c_str()).CStr());
            tex.Path.clear();
            tex.Path.append(new_path.c_str());
        }
    }
} // namespace ZEngine::Importers

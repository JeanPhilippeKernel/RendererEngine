#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <assimp/postprocess.h>
#include <fmt/format.h>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Meshes;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace uuids;

namespace fs = std::filesystem;

namespace ZEngine::Importers
{
    AssimpImporter::AssimpImporter() : m_progress_handler{}, m_flags{aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_SortByPType}
    {
        m_progress_handler.SetImporter(this);
    }

    AssimpImporter::~AssimpImporter() {}

    void AssimpImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(350), &Arena);
    }

    bool AssimpImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        return secure_strcmp(extension, "fbx") == 0 || secure_strcmp(extension, "obj") == 0;
    }

    Core::VFS::VFSResult<void> AssimpImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        // Resolve VFS path to native filesystem path for Assimp.
        // path is VFS-relative (e.g. /Assets/Lamp01.glb); Assimp needs the full native path.
        char        native[MAX_FILE_PATH_COUNT] = {};
        const char* working_space               = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
        if (working_space && working_space[0] != '\0')
            path.ResolveNative(working_space, native, sizeof(native));
        else
            path.ToNative(native, sizeof(native));

        Assimp::Importer importer{};
        const aiScene*   scene = importer.ReadFile(native, m_flags);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        {
            ZENGINE_CORE_ERROR("[AssimpImporter] Failed to read '{}': {}", native, importer.GetErrorString())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        Core::Memory::ArenaAllocator scratch{};
        Arena.CreateSubArena(ZMega(64), &scratch);

        std::random_device    rd;
        std::mt19937          generator(rd());
        uuid_random_generator gen(&generator);

        AssetMesh             mesh        = {};
        AssetNodeHierarchy    hierarchies = {};
        Array<AssetMaterial>  materials   = {};
        Array<AssetTexture>   textures    = {};

        ExtractMeshes(&scratch, scene, gen, mesh);
        mesh.MeshUUID = meta.AssetUUID; // stable UUID from .meta

        ExtractMaterials(&scratch, scene, gen, materials, hierarchies);
        ExtractTextures(&scratch, scene, gen, materials, textures);
        CreateHierachy(&scratch, scene, gen, hierarchies, mesh, materials);

        importer.SetProgressHandler(nullptr);
        importer.FreeScene();

        // Ingest directly into AssetManager CPU buffers — no intermediate .zasset round-trip.
        // Textures before materials (so UUIDToTextureHandle is populated for material GPU maps).
        // Mesh+Hierarchy last (triggers AssetRegistry::OnAssetReady → hot-reload cascade).
        if (Managers::AssetManager::Instance())
        {
            Managers::AssetManager::IngestTextures(std::move(textures));
            for (size_t i = 0; i < materials.size(); ++i)
                Managers::AssetManager::IngestMaterial(std::move(materials[i]));
            Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hierarchies));
        }

        Arena.Clear();
        return Core::VFS::VFSResult<void>::Ok();
    }

    void AssimpImporter::ImportFile(const char* filename, const AssetCodec::ImportConfiguration& cfg, Core::Memory::ArenaAllocator* arena, void* context, void (*on_complete)(void*, Core::Containers::ArrayView<AssetImporterOutput>), void (*on_progress)(void*, float), void (*on_error)(void*, std::string_view), void (*on_log)(void*, std::string_view))
    {
        AssetCodec::ImportConfiguration config = {};
        config.OutputWorkingSpacePath.init(arena, cfg.OutputWorkingSpacePath.c_str());
        config.OutputTextureFilesPath.init(arena, cfg.OutputTextureFilesPath.c_str());
        config.OutputAssetsPath.init(arena, cfg.OutputAssetsPath.c_str());
        config.AssetName.init(arena, cfg.AssetName.c_str());
        config.OutputAssetFile.init(arena, cfg.OutputAssetFile.c_str());
        config.InputBaseAssetFilePath.init(arena, cfg.InputBaseAssetFilePath.c_str());

        Assimp::Importer importer{};
        importer.SetProgressHandler(&m_progress_handler);

        const aiScene* scene = importer.ReadFile(filename, m_flags);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        {
            if (on_error)
                on_error(context, importer.GetErrorString());
        }
        else
        {
            std::random_device    rd;
            std::mt19937          generator(rd());
            uuid_random_generator gen(&generator);

            AssetMesh             mesh        = {};
            AssetNodeHierarchy    hierarchies = {};
            Array<AssetMaterial>  materials   = {};
            Array<AssetTexture>   textures    = {};

            ExtractMeshes(arena, scene, gen, mesh);
            ExtractMaterials(arena, scene, gen, materials, hierarchies);
            ExtractTextures(arena, scene, gen, materials, textures);
            CreateHierachy(arena, scene, gen, hierarchies, mesh, materials);
            CopyTextureFiles(arena, textures, config);

            // Propagate tex.Path (set by CopyTextureFiles) → material.*TexPath
            for (size_t m = 0; m < materials.size(); ++m)
            {
                auto set_path = [&](const uuids::uuid& uuid, Core::Containers::String& path_out) {
                    for (size_t t = 0; t < textures.size(); ++t)
                    {
                        if (textures[t].TextureUUID == uuid && !textures[t].Path.empty())
                        {
                            path_out.init(arena, textures[t].Path.c_str());
                            return;
                        }
                    }
                };
                set_path(materials[m].AlbedoTexUUID, materials[m].AlbedoTexPath);
                set_path(materials[m].EmissiveTexUUID, materials[m].EmissiveTexPath);
                set_path(materials[m].NormalTexUUID, materials[m].NormalTexPath);
                set_path(materials[m].OpacityTexUUID, materials[m].OpacityTexPath);
                set_path(materials[m].SpecularTexUUID, materials[m].SpecularTexPath);
            }

            // Serialize .zemesh + .zematerial — no .zetextures (paths are inline in material)
            Array<AssetImporterOutput> outputs = {};
            outputs.init(arena, 100);
            outputs.push(AssetCodec::SerializeMeshAssetFile(arena, mesh, hierarchies, config));
            for (size_t i = 0; i < materials.size(); ++i)
                outputs.push(AssetCodec::SerializeMaterialAssetFile(arena, materials[i], config));

            if (on_complete)
                on_complete(context, ArrayView{outputs});
        }

        importer.SetProgressHandler(nullptr);
        importer.FreeScene();
    }

    void AssimpImporter::ExtractMeshes(Core::Memory::ArenaAllocator* arena, const aiScene* scene, uuids::uuid_random_generator& generator, AssetMesh& mesh)
    {
        if ((!scene) || (!scene->HasMeshes()))
        {
            return;
        }

        unsigned int t_vertices = 0;
        unsigned int t_indices  = 0;
        unsigned int t_meshes   = scene->mNumMeshes;

        for (unsigned int i = 0; i < t_meshes; ++i)
        {
            aiMesh* mesh  = scene->mMeshes[i];
            t_vertices   += mesh->mNumVertices;
            t_indices    += mesh->mNumFaces * 3; // assuming triangulated
        }

        mesh.MeshUUID = generator();
        mesh.SubMeshes.init(arena, t_meshes, t_meshes);
        mesh.Vertices.init(arena, t_vertices);
        mesh.Indices.init(arena, t_indices);

        uint32_t VertexOffset = 0;
        uint32_t IndexOffset  = 0;

        for (uint32_t m = 0; m < t_meshes; ++m)
        {
            ZENGINE_CORE_INFO("{}", fmt::format("Extrating Meshes : {0}/{1} ", (m + 1), t_meshes))

            aiMesh*  ai_mesh = scene->mMeshes[m];

            uint32_t vertex_count{0};

            /* Vertice processing */
            for (int v = 0; v < ai_mesh->mNumVertices; ++v)
            {
                const aiVector3D position = ai_mesh->mVertices[v];
                const aiVector3D normal   = ai_mesh->mNormals[v];
                const aiVector3D texture  = ai_mesh->HasTextureCoords(0) ? ai_mesh->mTextureCoords[0][v] : aiVector3D{};

                mesh.Vertices.push(position.x);
                mesh.Vertices.push(position.y);
                mesh.Vertices.push(position.z);

                mesh.Vertices.push(normal.x);
                mesh.Vertices.push(normal.y);
                mesh.Vertices.push(normal.z);

                mesh.Vertices.push(texture.x);
                mesh.Vertices.push(texture.y);

                vertex_count++;
            }

            /* Face and Indices processing */
            uint32_t index_count{0};
            for (int f = 0; f < ai_mesh->mNumFaces; ++f)
            {
                aiFace ai_face = ai_mesh->mFaces[f];

                for (int fidx = 0; fidx < ai_face.mNumIndices; ++fidx)
                {
                    mesh.Indices.push(ai_face.mIndices[fidx]);

                    index_count++;
                }
            }

            auto& subMesh                 = mesh.SubMeshes[m];
            subMesh.VertexCount           = vertex_count;
            subMesh.VertexOffset          = VertexOffset;
            subMesh.VertexUnitStreamSize  = sizeof(float) * (3 + 3 + 2) /*pos-cmp + normal-cmp + tex-cmp*/;
            subMesh.StreamOffset          = (subMesh.VertexUnitStreamSize * subMesh.VertexOffset);
            subMesh.IndexOffset           = IndexOffset;
            subMesh.IndexCount            = index_count;
            subMesh.IndexUnitStreamSize   = sizeof(uint32_t);
            subMesh.IndexStreamOffset     = (subMesh.IndexUnitStreamSize * subMesh.IndexOffset);
            subMesh.TotalByteSize         = (subMesh.VertexCount * subMesh.VertexUnitStreamSize) + (subMesh.IndexCount * subMesh.IndexUnitStreamSize);

            /* Computing offset data */
            VertexOffset                 += ai_mesh->mNumVertices;
            IndexOffset                  += index_count;
        }
    }

    void AssimpImporter::ExtractMaterials(Core::Memory::ArenaAllocator* arena, const aiScene* scene, uuids::uuid_random_generator& generator, Core::Containers::Array<AssetMaterial>& materials, AssetNodeHierarchy& model)
    {
        if (!scene)
        {
            return;
        }

        uint32_t number_of_materials = scene->mNumMaterials;
        materials.init(arena, number_of_materials, number_of_materials);
        model.MaterialNames.init(arena, number_of_materials, number_of_materials);

        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            ZENGINE_CORE_INFO("{}", fmt::format("Extrating materials : {0}/{1}", (m + 1), number_of_materials))

            aiColor4D      color;
            aiMaterial*    ai_material = scene->mMaterials[m];
            aiString       mat_name    = ai_material->GetName();
            AssetMaterial& material    = materials[m];

            material.MaterialUUID      = generator();

            {
                std::stringstream ss;
                ss << material.MaterialUUID;
                auto mat_uuid = ss.str();
                material.Name.init(arena, (mat_name.C_Str() ? mat_name.C_Str() : mat_uuid.c_str()));
                ss.clear();
            }

            model.MaterialNames[m].init(arena, material.Name.c_str());

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS)
            {
                material.AmbientColor[0] = color.r;
                material.AmbientColor[1] = color.g;
                material.AmbientColor[2] = color.b;
                material.AmbientColor[3] = color.a;
                material.AmbientColor[3] = min(material.AmbientColor[3], 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
            {
                material.AlbedoColor[0] = color.r;
                material.AlbedoColor[1] = color.g;
                material.AlbedoColor[2] = color.b;
                material.AlbedoColor[3] = color.a;
                material.AlbedoColor[3] = min(material.AlbedoColor[3], 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
            {
                material.SpecularColor[0] = color.r;
                material.SpecularColor[1] = color.g;
                material.SpecularColor[2] = color.b;
                material.SpecularColor[3] = color.a;
                material.SpecularColor[3] = min(material.SpecularColor[3], 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS)
            {
                material.EmissiveColor[0] = color.r;
                material.EmissiveColor[1] = color.g;
                material.EmissiveColor[2] = color.b;
                material.EmissiveColor[3] = color.a;
                material.EmissiveColor[3] = min(material.EmissiveColor[3], 1.0f);
            }

            float       opacity              = 1.0f;
            const float opaqueness_threshold = 0.05f;

            if (aiGetMaterialFloat(ai_material, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS)
            {
                material.Factors[0] = clamp(1.f - opacity, 0.0f, 1.0f);
                if (material.Factors[0] >= (1.0f - opaqueness_threshold))
                {
                    material.Factors[0] = 0.0f;
                }
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_TRANSPARENT, &color) == AI_SUCCESS)
            {
                const float component_as_opacity = std::max(std::max(color.r, color.g), color.b);
                material.Factors[0]              = clamp(component_as_opacity, 0.0f, 1.0f);
                if (material.Factors[0] >= (1.0f - opaqueness_threshold))
                {
                    material.Factors[0] = 0.0f;
                }
                material.Factors[2] = 0.5f;
            }
        }
    }

    void AssimpImporter::ExtractTextures(Core::Memory::ArenaAllocator* arena, const aiScene* scene, uuids::uuid_random_generator& generator, Core::Containers::Array<AssetMaterial>& materials, Core::Containers::Array<AssetTexture>& textures)
    {
        if (!scene)
        {
            return;
        }

        aiString         texture_filename;
        aiTextureMapping texture_mapping;
        uint32_t         uv_index;
        float            blend               = 1.0f;
        aiTextureOp      texture_operation   = aiTextureOp_Add;
        aiTextureMapMode texture_map_mode[]  = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        uint32_t         texture_flags       = 0;

        uint32_t         number_of_materials = scene->mNumMaterials;

        textures.init(arena, number_of_materials);

        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            ZENGINE_CORE_INFO("{}", fmt::format("Extrating Material's textures:  {0}/{1} materials", (m + 1), number_of_materials))

            aiMaterial*    ai_material = scene->mMaterials[m];
            AssetMaterial& material    = materials[m];

            if (aiGetMaterialTexture(ai_material, aiTextureType_DIFFUSE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                AssetTexture& texture  = textures.push_use({});
                texture.TextureUUID    = generator();
                material.AlbedoTexUUID = texture.TextureUUID;
                texture.Path.init(arena, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_SPECULAR, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                AssetTexture& texture    = textures.push_use({});
                texture.TextureUUID      = generator();
                material.SpecularTexUUID = texture.TextureUUID;
                texture.Path.init(arena, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_EMISSIVE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                AssetTexture& texture    = textures.push_use({});
                texture.TextureUUID      = generator();
                material.EmissiveTexUUID = texture.TextureUUID;
                texture.Path.init(arena, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_NORMALS, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                AssetTexture& texture  = textures.push_use({});
                texture.TextureUUID    = generator();
                material.NormalTexUUID = texture.TextureUUID;
                texture.Path.init(arena, texture_filename.C_Str());
            }

            if (material.NormalTexUUID.is_nil())
            {
                if (aiGetMaterialTexture(ai_material, aiTextureType_HEIGHT, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
                {
                    AssetTexture& texture  = textures.push_use({});
                    texture.TextureUUID    = generator();
                    material.NormalTexUUID = texture.TextureUUID;
                    texture.Path.init(arena, texture_filename.C_Str());
                }
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_OPACITY, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                AssetTexture& texture   = textures.push_use({});
                texture.TextureUUID     = generator();
                material.OpacityTexUUID = texture.TextureUUID;
                texture.Path.init(arena, texture_filename.C_Str());
                material.Factors[2] = 0.5f;
            }
        }
    }

    void AssimpImporter::CreateHierachy(Core::Memory::ArenaAllocator* arena, const aiScene* scene, uuids::uuid_random_generator& generator, AssetNodeHierarchy& AssetNode, AssetMesh& asset_mesh, Core::Containers::Array<AssetMaterial>& materials)
    {
        if (!scene || !(scene->mRootNode))
        {
            return;
        }

        AssetNode.NodeHierarchyUUID = generator();
        AssetNode.MeshUUID          = asset_mesh.MeshUUID;

        AssetNode.Hierarchies.init(arena, 3000);
        AssetNode.LocalTransforms.init(arena, 3000);
        AssetNode.GlobalTransforms.init(arena, 3000);
        AssetNode.Names.init(arena, 3000);
        AssetNode.NodeNames.init(arena, 3000);
        AssetNode.NodeMeshes.init(arena, 3000);
        AssetNode.NodeMaterials.init(arena, 3000);

        TraverseNode(arena, scene, scene->mRootNode, AssetNode, asset_mesh, materials, -1, 0);
    }

    void AssimpImporter::TraverseNode(Core::Memory::ArenaAllocator* arena, const aiScene* ai_scene, const aiNode* node, AssetNodeHierarchy& hierarchy, AssetMesh& asset_mesh, Core::Containers::Array<AssetMaterial>& materials, int parent_node_id, int depth_level)
    {
        auto node_id                 = AddNode(hierarchy, parent_node_id, depth_level);
        hierarchy.NodeNames[node_id] = hierarchy.Names.size();
        auto& name                   = hierarchy.Names.push_use({});
        name.init(arena, node->mName.C_Str() ? node->mName.C_Str() : "<unamed node>");

        hierarchy.GlobalTransforms[node_id] = Identity<Mat4f>();
        hierarchy.LocalTransforms[node_id]  = ConvertToMat4(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            auto     sub_node_id             = AddNode(hierarchy, node_id, depth_level + 1);
            uint32_t mesh                    = node->mMeshes[i];
            uint32_t material_id             = ai_scene->mMeshes[mesh]->mMaterialIndex;

            hierarchy.NodeNames[sub_node_id] = hierarchy.Names.size();
            auto&    n                       = hierarchy.Names.push_use({});
            aiString mesh_name               = ai_scene->mMeshes[mesh]->mName;
            n.init(arena, mesh_name.C_Str() ? mesh_name.C_Str() : "<unamed node>");

            hierarchy.NodeMeshes[sub_node_id]       = mesh;
            hierarchy.NodeMaterials[sub_node_id]    = material_id;
            hierarchy.GlobalTransforms[sub_node_id] = Identity<Mat4f>();
            hierarchy.LocalTransforms[sub_node_id]  = Identity<Mat4f>();

            auto& asset_mat                         = materials[material_id];
            auto& sub_mesh                          = asset_mesh.SubMeshes[mesh];
            sub_mesh.MaterialUUID                   = asset_mat.MaterialUUID;
        }

        for (uint32_t child = 0; child < node->mNumChildren; ++child)
        {
            TraverseNode(arena, ai_scene, node->mChildren[child], hierarchy, asset_mesh, materials, node_id, (depth_level + 1));
        }
    }

    void AssimpImporter::CopyTextureFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::Array<AssetTexture>& textures, const AssetCodec::ImportConfiguration& config)
    {
        /*
         * Normalize file naming
         */
        auto dst_dir               = fmt::format("{0}{1}{2}{3}{4}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputTextureFilesPath.c_str(), PLATFORM_OS_BACKSLASH, config.AssetName.c_str());

        auto CreateBaseDirectoryFn = [](std::string_view filename) -> void {
            auto            base_dir = fs::absolute(filename).parent_path();

            std::error_code err      = {};
            if (!fs::exists(base_dir))
            {
                fs::create_directories(base_dir, err);
            }
        };

        auto          scratch       = ZGetScratch(arena);
        Array<String> src_tex_files = {};
        Array<String> dst_tex_files = {};
        src_tex_files.init(scratch.Arena, textures.size());
        dst_tex_files.init(scratch.Arena, textures.size());

        for (auto& tex : textures)
        {
            if (tex.Path.empty())
            {
                continue;
            }

            auto src_file = fmt::format("{0}{1}{2}", config.InputBaseAssetFilePath.c_str(), PLATFORM_OS_BACKSLASH, tex.Path.c_str());
            auto dst_file = fmt::format("{0}{1}{2}", dst_dir, PLATFORM_OS_BACKSLASH, tex.Path.c_str());

            CreateBaseDirectoryFn(dst_file);

            auto& sf = src_tex_files.push_use({});
            auto& df = dst_tex_files.push_use({});

            sf.init(scratch.Arena, src_file.c_str());
            df.init(scratch.Arena, dst_file.c_str());
        }
        /*
         * Texture files processing
         *  (1) Ensuring Scene sub-dir is created
         *  (2) Copying files to destination
         */

        ZENGINE_VALIDATE_ASSERT(src_tex_files.size() == dst_tex_files.size(), "source files count can't be diff of destination files count")
        for (int i = 0; i < src_tex_files.size(); ++i)
        {
            auto          src = fs::absolute(src_tex_files[i].c_str());
            auto          dst = fs::absolute(dst_tex_files[i].c_str());

            std::ifstream in(src.c_str(), std::ios::binary);
            std::ofstream out(dst.c_str(), std::ios::binary);

            if (!in.is_open() || !out.is_open())
            {
                in.close();
                out.close();
                continue;
            }

            out << in.rdbuf();

            in.close();
            out.close();
        }

        ZReleaseScratch(scratch);

        /*
         * Update texture path
         */
        for (auto& tex : textures)
        {
            auto new_path = fmt::format("{0}{1}{2}{3}{4}", config.OutputTextureFilesPath.c_str(), PLATFORM_OS_BACKSLASH, config.AssetName.c_str(), PLATFORM_OS_BACKSLASH, tex.Path.c_str());
            tex.Path.clear();
            tex.Path.append(new_path.c_str());
        }
    }

    Mat4f AssimpImporter::ConvertToMat4(const aiMatrix4x4& m)
    {
        Mat4f mm;
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                mm(i, j) = m[i][j];
            }
        }
        return mm;
    }

    void AssimpProgressHandler::SetImporter(AssimpImporter* const importer)
    {
        m_importer = importer;
    }

    bool AssimpProgressHandler::Update(float /*percentage*/)
    {
        return true;
    }
} // namespace ZEngine::Importers

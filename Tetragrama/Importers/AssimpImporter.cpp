#include <pch.h>
#include <AssimpImporter.h>
#include <Core/Coroutine.h>
#include <Helpers/MemoryOperations.h>
#include <Helpers/SerializerCommonHelper.h>
#include <Helpers/ThreadPool.h>
#include <assimp/postprocess.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;
using namespace Tetragrama::Helpers;
using namespace ZEngine::Rendering::Meshes;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Core::Containers;

namespace fs = std::filesystem;

namespace Tetragrama::Importers
{
    AssimpImporter::AssimpImporter() : m_progress_handler{}, m_flags{aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_SortByPType}
    {
        m_progress_handler.SetImporter(this);
    }

    AssimpImporter::~AssimpImporter() {}

    std::future<void> AssimpImporter::ImportAsync(std::string_view filename, ImportConfiguration config)
    {
        ThreadPoolHelper::Submit([this, path = std::string(filename.data()), config] {
            std::unique_lock l(m_mutex);
            Arena.Clear();
            m_is_importing.store(true, std::memory_order_release);

            Assimp::Importer importer{};
            importer.SetProgressHandler(&m_progress_handler);

            const aiScene* scene = importer.ReadFile(path, m_flags);

            if ((!scene) || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            {
                if (m_error_callback)
                {
                    m_error_callback(Context, importer.GetErrorString());
                }
            }
            else
            {
                ImporterData import_data = {};

                ExtractMeshes(scene, import_data);
                ExtractMaterials(scene, import_data);
                ExtractTextures(scene, import_data);
                CreateHierachyScene(scene, import_data);
                /*
                 * Serialization of ImporterData
                 */
                REPORT_LOG(Context, "Serializing model...")
                SerializeImporterData(&Arena, import_data, config);

                if (m_complete_callback)
                {
                    m_complete_callback(Context, std::move(import_data));
                }
            }

            importer.SetProgressHandler(nullptr);
            importer.FreeScene();

            m_is_importing.store(false, std::memory_order_release);
        });

        co_return;
    }

    void AssimpImporter::ExtractMeshes(const aiScene* scene, ImporterData& importer_data)
    {
        if ((!scene) || (!scene->HasMeshes()))
        {
            return;
        }

        uint32_t number_of_meshes = scene->mNumMeshes;
        importer_data.Scene.Meshes.reserve(number_of_meshes);

        for (uint32_t m = 0; m < number_of_meshes; ++m)
        {

            REPORT_LOG(Context, fmt::format("Extrating Meshes : {0}/{1} ", (m + 1), number_of_meshes).c_str())

            aiMesh*  ai_mesh = scene->mMeshes[m];

            uint32_t vertex_count{0};

            /* Vertice processing */
            for (int v = 0; v < ai_mesh->mNumVertices; ++v)
            {
                const aiVector3D position = ai_mesh->mVertices[v];
                const aiVector3D normal   = ai_mesh->mNormals[v];
                const aiVector3D texture  = ai_mesh->HasTextureCoords(0) ? ai_mesh->mTextureCoords[0][v] : aiVector3D{};

                importer_data.Scene.Vertices.push_back(position.x);
                importer_data.Scene.Vertices.push_back(position.y);
                importer_data.Scene.Vertices.push_back(position.z);

                importer_data.Scene.Vertices.push_back(normal.x);
                importer_data.Scene.Vertices.push_back(normal.y);
                importer_data.Scene.Vertices.push_back(normal.z);

                importer_data.Scene.Vertices.push_back(texture.x);
                importer_data.Scene.Vertices.push_back(texture.y);

                vertex_count++;
            }

            /* Face and Indices processing */
            uint32_t index_count{0};
            for (int f = 0; f < ai_mesh->mNumFaces; ++f)
            {
                aiFace ai_face = ai_mesh->mFaces[f];

                for (int fidx = 0; fidx < ai_face.mNumIndices; ++fidx)
                {
                    importer_data.Scene.Indices.push_back(ai_face.mIndices[fidx]);

                    index_count++;
                }
            }

            MeshVNext& mesh             = importer_data.Scene.Meshes.emplace_back();
            mesh.VertexCount            = vertex_count;
            mesh.VertexOffset           = importer_data.VertexOffset;
            mesh.VertexUnitStreamSize   = sizeof(float) * (3 + 3 + 2) /*pos-cmp + normal-cmp + tex-cmp*/;
            mesh.StreamOffset           = (mesh.VertexUnitStreamSize * mesh.VertexOffset);
            mesh.IndexOffset            = importer_data.IndexOffset;
            mesh.IndexCount             = index_count;
            mesh.IndexUnitStreamSize    = sizeof(uint32_t);
            mesh.IndexStreamOffset      = (mesh.IndexUnitStreamSize * mesh.IndexOffset);
            mesh.TotalByteSize          = (mesh.VertexCount * mesh.VertexUnitStreamSize) + (mesh.IndexCount * mesh.IndexUnitStreamSize);

            /* Computing offset data */
            importer_data.VertexOffset += ai_mesh->mNumVertices;
            importer_data.IndexOffset  += index_count;
        }
    }

    void AssimpImporter::ExtractMaterials(const aiScene* scene, ImporterData& importer_data)
    {
        if (!scene)
        {
            return;
        }

        uint32_t number_of_materials = scene->mNumMaterials;
        importer_data.Scene.Materials.resize(number_of_materials);
        importer_data.Scene.MaterialFiles.resize(number_of_materials);
        importer_data.Scene.MaterialNames.resize(number_of_materials);

        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            REPORT_LOG(Context, fmt::format("Extrating materials : {0}/{1}", (m + 1), number_of_materials).c_str())

            aiColor4D     color;
            aiMaterial*   ai_material            = scene->mMaterials[m];
            aiString      mat_name               = ai_material->GetName();
            MeshMaterial& material               = importer_data.Scene.Materials[m];

            importer_data.Scene.MaterialNames[m] = (mat_name.C_Str() ? std::string(mat_name.C_Str()) : std::string("<unamed material>"));

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS)
            {
                material.AmbientColor   = ZEngine::Rendering::gpuvec4{color.r, color.g, color.b, color.a};
                material.AmbientColor.w = glm::min(material.AmbientColor.w, 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
            {
                material.AlbedoColor   = ZEngine::Rendering::gpuvec4{color.r, color.g, color.b, color.a};
                material.AlbedoColor.w = std::min(material.AlbedoColor.w, 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
            {
                material.SpecularColor   = ZEngine::Rendering::gpuvec4{color.r, color.g, color.b, color.a};
                material.SpecularColor.w = std::min(material.SpecularColor.w, 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS)
            {
                material.EmissiveColor   = ZEngine::Rendering::gpuvec4{color.r, color.g, color.b, color.a};
                material.EmissiveColor.w = std::min(material.EmissiveColor.w, 1.0f);
            }

            float       opacity              = 1.0f;
            const float opaqueness_threshold = 0.05f;

            if (aiGetMaterialFloat(ai_material, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS)
            {
                material.Factors.x = glm::clamp(1.f - opacity, 0.0f, 1.0f);
                if (material.Factors.x >= (1.0f - opaqueness_threshold))
                {
                    material.Factors.x = 0.0f;
                }
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_TRANSPARENT, &color) == AI_SUCCESS)
            {
                const float component_as_opacity = std::max(std::max(color.r, color.g), color.b);
                material.Factors.x               = glm::clamp(component_as_opacity, 0.0f, 1.0f);
                if (material.Factors.x >= (1.0f - opaqueness_threshold))
                {
                    material.Factors.x = 0.0f;
                }
                material.Factors.z = 0.5f;
            }
        }
    }

    void AssimpImporter::ExtractTextures(const aiScene* scene, ImporterData& importer_data)
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
        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            REPORT_LOG(Context, fmt::format("Extrating Material's textures:  {0}/{1} materials", (m + 1), number_of_materials).c_str())

            aiMaterial* ai_material = scene->mMaterials[m];

            if (aiGetMaterialTexture(ai_material, aiTextureType_DIFFUSE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                secure_strcpy(importer_data.Scene.MaterialFiles[m].AlbedoTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_SPECULAR, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                secure_strcpy(importer_data.Scene.MaterialFiles[m].SpecularTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_EMISSIVE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                secure_strcpy(importer_data.Scene.MaterialFiles[m].EmissiveTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_NORMALS, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                secure_strcpy(importer_data.Scene.MaterialFiles[m].NormalTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
            }

            if (importer_data.Scene.Materials[m].NormalMap == 0xFFFFFFFF)
            {
                if (aiGetMaterialTexture(ai_material, aiTextureType_HEIGHT, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
                {
                    secure_strcpy(importer_data.Scene.MaterialFiles[m].NormalTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
                }
            }

            if (aiGetMaterialTexture(ai_material, aiTextureType_OPACITY, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) == AI_SUCCESS)
            {
                secure_strcpy(importer_data.Scene.MaterialFiles[m].OpacityTexture, MAX_FILE_PATH_COUNT, texture_filename.C_Str());
                importer_data.Scene.Materials[m].Factors.z = 0.5f;
            }
        }
    }

    void AssimpImporter::CreateHierachyScene(const aiScene* scene, ImporterData& importer_data)
    {
        if (!scene || !(scene->mRootNode))
        {
            return;
        }

        TraverseNode(scene, &(importer_data.Scene), scene->mRootNode, -1, 0);
    }

    void AssimpImporter::TraverseNode(const aiScene* ai_scene, SceneRawData* const scene, const aiNode* node, int parent_node_id, int depth_level)
    {
        auto node_id              = scene->AddNode(parent_node_id, depth_level);
        scene->NodeNames[node_id] = scene->Names.size();
        scene->Names.push_back(node->mName.C_Str() ? std::string(node->mName.C_Str()) : std::string{"<unamed node>"});

        scene->GlobalTransforms[node_id] = glm::mat4(1.0f);
        scene->LocalTransforms[node_id]  = ConvertToMat4(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            auto     sub_node_id          = scene->AddNode(node_id, depth_level + 1);
            uint32_t mesh                 = node->mMeshes[i];
            aiString mesh_name            = ai_scene->mMeshes[mesh]->mName;

            scene->NodeNames[sub_node_id] = scene->Names.size();
            scene->Names.push_back(mesh_name.C_Str() ? std::string(mesh_name.C_Str()) : std::string{"<unamed node>"});

            scene->NodeMeshes[sub_node_id]       = mesh;
            scene->NodeMaterials[sub_node_id]    = ai_scene->mMeshes[mesh]->mMaterialIndex;
            scene->GlobalTransforms[sub_node_id] = glm::mat4(1.0f);
            scene->LocalTransforms[sub_node_id]  = glm::mat4(1.0f);
        }

        for (uint32_t child = 0; child < node->mNumChildren; ++child)
        {
            TraverseNode(ai_scene, scene, node->mChildren[child], node_id, (depth_level + 1));
        }
    }

    void AssimpImporter::SerializeImporterData(ZEngine::Core::Memory::ArenaAllocator* arena, ImporterData& importer_data, const ImportConfiguration& config)
    {
        importer_data.Name = config.AssetFilename;

        if (!config.OutputMeshFilePath.empty())
        {
            std::string   output_mesh_file = fmt::format("{}.zemeshes", config.AssetFilename.c_str());
            std::string   fullname_path    = fmt::format("{0}/{1}/{2}", config.OutputWorkingSpacePath.c_str(), config.OutputMeshFilePath.c_str(), output_mesh_file.c_str());
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t mesh_count = importer_data.Scene.Meshes.size();
                out.write(reinterpret_cast<const char*>(&mesh_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Meshes.data()), sizeof(ZEngine::Rendering::Meshes::MeshVNext) * mesh_count);

                size_t indices_count = importer_data.Scene.Indices.size();
                out.write(reinterpret_cast<const char*>(&indices_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Indices.data()), sizeof(uint32_t) * indices_count);

                size_t vertice_count = importer_data.Scene.Vertices.size();
                out.write(reinterpret_cast<const char*>(&vertice_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Vertices.data()), sizeof(float) * vertice_count);
            }
            out.close();

            importer_data.SerializedMeshesPath.init(arena, output_mesh_file.c_str());
        }

        if (!config.OutputMaterialsPath.empty() && !config.OutputTextureFilesPath.empty())
        {
            /*
             * Normalize file naming
             */
            auto dst_dir            = fmt::format("{0}/{1}/{2}", config.OutputWorkingSpacePath.c_str(), config.OutputTextureFilesPath.c_str(), config.AssetFilename.c_str());

            auto create_base_dir_fn = [](std::string_view filename) -> void {
                auto            base_dir = fs::absolute(filename).parent_path();

                std::error_code err      = {};
                if (!fs::exists(base_dir))
                {
                    fs::create_directories(base_dir, err);
                }
            };

            std::vector<std::string> src_tex_files = {};
            std::vector<std::string> dst_tex_files = {};
            for (auto& mat_file : importer_data.Scene.MaterialFiles)
            {
                if (!std::string_view(mat_file.AlbedoTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath.c_str(), mat_file.AlbedoTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.AlbedoTexture);
                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.AlbedoTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.EmissiveTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath.c_str(), mat_file.EmissiveTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.EmissiveTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.EmissiveTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.NormalTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath.c_str(), mat_file.NormalTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.NormalTexture);

                    create_base_dir_fn(dst_file);
                    ZEngine::Helpers::secure_strcpy(mat_file.NormalTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.OpacityTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath.c_str(), mat_file.OpacityTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.OpacityTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.OpacityTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.SpecularTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath.c_str(), mat_file.SpecularTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.SpecularTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.SpecularTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }
            }
            /*
             * Texture files processing
             *  (1) Ensuring Scene sub-dir is created
             *  (2) Copying files to destination
             */

            ZENGINE_VALIDATE_ASSERT(src_tex_files.size() == dst_tex_files.size(), "source files count can't be diff of destination files count")
            for (int i = 0; i < src_tex_files.size(); ++i)
            {
                auto          src = fs::absolute(src_tex_files[i]);
                auto          dst = fs::absolute(dst_tex_files[i]);

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

            std::string   output_material_file = fmt::format("{}.zematerials", config.AssetFilename.c_str());
            std::string   fullname_path        = fmt::format("{0}/{1}/{2}", config.OutputWorkingSpacePath.c_str(), config.OutputMaterialsPath.c_str(), output_material_file.c_str());
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t material_total_count = importer_data.Scene.Materials.size();
                out.write(reinterpret_cast<const char*>(&material_total_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Materials.data()), sizeof(ZEngine::Rendering::Meshes::MeshMaterial) * material_total_count);

                size_t mat_file_count = importer_data.Scene.MaterialFiles.size();
                out.write(reinterpret_cast<const char*>(&mat_file_count), sizeof(size_t));
                for (auto& mat_file : importer_data.Scene.MaterialFiles)
                {
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.AlbedoTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.EmissiveTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.NormalTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.OpacityTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.SpecularTexture);
                }
            }
            out.close();

            importer_data.SerializedMaterialsPath.init(arena, output_material_file.c_str());
        }

        if (!config.OutputModelFilePath.empty())
        {
            std::string   output_model_file = fmt::format("{}.zemodel", config.AssetFilename.c_str());
            std::string   fullname_path     = fmt::format("{0}/{1}/{2}", config.OutputWorkingSpacePath.c_str(), config.OutputModelFilePath.c_str(), output_model_file.c_str());
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t local_transform_count = importer_data.Scene.LocalTransforms.size();
                out.write(reinterpret_cast<const char*>(&local_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.LocalTransforms.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count = importer_data.Scene.GlobalTransforms.size();
                out.write(reinterpret_cast<const char*>(&gobal_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.GlobalTransforms.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count = importer_data.Scene.NodeHierarchies.size();
                out.write(reinterpret_cast<const char*>(&node_hierarchy_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.NodeHierarchies.data()), sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

                // Todo Kernel : This should be part of Move Engine to Arena
                //
                // Tetragrama::Helpers::SerializeStringArrayData(out, importer_data.Scene.Names);
                // Tetragrama::Helpers::SerializeStringArrayData(out, importer_data.Scene.MaterialNames);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeNames);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeMeshes);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeMaterials);
            }

            out.close();

            importer_data.SerializedModelPath.init(arena, output_model_file.c_str());
        }
    }

    ImporterData AssimpImporter::DeserializeImporterData(ZEngine::Core::Memory::ArenaAllocator* arena, std::string_view model_path, std::string_view mesh_path, std::string_view material_path)
    {
        ImporterData deserialized_data = {};

        if (!mesh_path.empty())
        {
            std::ifstream in(mesh_path.data(), std::ios::binary);

            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t mesh_count;
                in.read(reinterpret_cast<char*>(&mesh_count), sizeof(size_t));
                deserialized_data.Scene.Meshes.resize(mesh_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Meshes.data()), sizeof(ZEngine::Rendering::Meshes::MeshVNext) * mesh_count);

                size_t indices_count;
                in.read(reinterpret_cast<char*>(&indices_count), sizeof(size_t));
                deserialized_data.Scene.Indices.resize(indices_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Indices.data()), sizeof(uint32_t) * indices_count);

                size_t vertice_count;
                in.read(reinterpret_cast<char*>(&vertice_count), sizeof(size_t));
                deserialized_data.Scene.Vertices.resize(vertice_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Vertices.data()), sizeof(float) * vertice_count);
            }
            in.close();
        }

        if (!material_path.empty())
        {
            std::ifstream in(material_path.data(), std::ios::binary);

            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t material_total_count;
                in.read(reinterpret_cast<char*>(&material_total_count), sizeof(size_t));
                deserialized_data.Scene.Materials.resize(material_total_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Materials.data()), sizeof(ZEngine::Rendering::Meshes::MeshMaterial) * material_total_count);

                size_t mat_file_count;
                in.read(reinterpret_cast<char*>(&mat_file_count), sizeof(size_t));
                deserialized_data.Scene.MaterialFiles.resize(mat_file_count);

                Array<String> textures = {};
                textures.init(arena, 5, 5);
                for (auto& mat_file : deserialized_data.Scene.MaterialFiles)
                {
                    Tetragrama::Helpers::DeserializeStringData(arena, in, textures[0]);
                    Tetragrama::Helpers::DeserializeStringData(arena, in, textures[1]);
                    Tetragrama::Helpers::DeserializeStringData(arena, in, textures[2]);
                    Tetragrama::Helpers::DeserializeStringData(arena, in, textures[3]);
                    Tetragrama::Helpers::DeserializeStringData(arena, in, textures[4]);

                    ZEngine::Helpers::secure_strcpy(mat_file.AlbedoTexture, MAX_FILE_PATH_COUNT, textures[0].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.EmissiveTexture, MAX_FILE_PATH_COUNT, textures[1].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.NormalTexture, MAX_FILE_PATH_COUNT, textures[2].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.OpacityTexture, MAX_FILE_PATH_COUNT, textures[3].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.SpecularTexture, MAX_FILE_PATH_COUNT, textures[4].c_str());
                }
            }
            in.close();
        }

        if (!model_path.empty())
        {
            std::ifstream in(model_path.data(), std::ios::binary);
            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t local_transform_count;
                in.read(reinterpret_cast<char*>(&local_transform_count), sizeof(size_t));
                deserialized_data.Scene.LocalTransforms.resize(local_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.LocalTransforms.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count;
                in.read(reinterpret_cast<char*>(&gobal_transform_count), sizeof(size_t));
                deserialized_data.Scene.GlobalTransforms.resize(gobal_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.GlobalTransforms.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count;
                in.read(reinterpret_cast<char*>(&node_hierarchy_count), sizeof(size_t));
                deserialized_data.Scene.NodeHierarchies.resize(node_hierarchy_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.NodeHierarchies.data()), sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

                // Todo Kernel : This should be part of Move Engine to Arena
                //
                // DeserializeStringArrayData(arena, in, deserialized_data.Scene.Names);
                // DeserializeStringArrayData(arena, in, deserialized_data.Scene.MaterialNames);
                DeserializeMapData(arena, in, deserialized_data.Scene.NodeNames);
                DeserializeMapData(arena, in, deserialized_data.Scene.NodeMeshes);
                DeserializeMapData(arena, in, deserialized_data.Scene.NodeMaterials);
            }
            in.close();
        }

        return deserialized_data;
    }

    glm::mat4 AssimpImporter::ConvertToMat4(const aiMatrix4x4& m)
    {
        glm::mat4 mm;
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                mm[i][j] = m[i][j];
            }
        }
        return mm;
    }

    void AssimpProgressHandler::SetImporter(AssimpImporter* const importer)
    {
        m_importer = importer;
    }

    bool AssimpProgressHandler::Update(float percentage)
    {
        if (m_importer && m_importer->m_progress_callback)
        {
            m_importer->m_progress_callback(m_importer->Context, percentage);
        }
        return true;
    }
} // namespace Tetragrama::Importers
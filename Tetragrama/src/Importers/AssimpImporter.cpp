#include <pch.h>
#include <AssimpImporter.h>
#include <assimp/postprocess.h>
#include <Core/Coroutine.h>
#include <assimp/scene.h>

#include <Helpers/ThreadPool.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Meshes;

namespace Tetragrama::Importers
{
    AssimpImporter::AssimpImporter()
        : m_progress_handler{}, m_flags{
                                    aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_SplitLargeMeshes |
                                    aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials | aiProcess_GenUVCoords | aiProcess_FlipUVs |
                                    aiProcess_ValidateDataStructure | aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_LimitBoneWeights}
    {
        m_progress_handler.SetImporter(this);
    }

    AssimpImporter::~AssimpImporter()
    {
    }

    std::future<void> AssimpImporter::ImportAsync(std::string_view filename, ImportConfiguration config)
    {
        ThreadPoolHelper::Submit([this, path = std::string(filename.data()), config] {
            {
                std::unique_lock l(m_mutex);
                m_is_importing = true;
            }

            Assimp::Importer importer{};
            importer.SetProgressHandler(&m_progress_handler);

            const aiScene* scene = importer.ReadFile(path, m_flags);

            if ((!scene) || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            {
                if (m_error_callback)
                {
                    m_error_callback(importer.GetErrorString());
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
                REPORT_LOG("Serializing model...")
                SerializeImporterData(import_data, config);

                if (m_complete_callback)
                {
                    m_complete_callback(std::move(import_data));
                }
            }

            importer.SetProgressHandler(nullptr);
            importer.FreeScene();

            {
                std::unique_lock l(m_mutex);
                m_is_importing = false;
            }
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

            REPORT_LOG(fmt::format("Extrating Meshes : {0}/{1} ", (m + 1), number_of_meshes).c_str())

            aiMesh* ai_mesh = scene->mMeshes[m];

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

            MeshVNext& mesh           = importer_data.Scene.Meshes.emplace_back();
            mesh.VertexCount          = vertex_count;
            mesh.VertexOffset         = importer_data.VertexOffset;
            mesh.VertexUnitStreamSize = sizeof(float) * (3 + 3 + 2) /*pos-cmp + normal-cmp + tex-cmp*/;
            mesh.StreamOffset         = (mesh.VertexUnitStreamSize * mesh.VertexOffset);
            mesh.IndexOffset          = importer_data.IndexOffset;
            mesh.IndexCount           = index_count;
            mesh.IndexUnitStreamSize  = sizeof(uint32_t);
            mesh.IndexStreamOffset    = (mesh.IndexUnitStreamSize * mesh.IndexOffset);
            mesh.TotalByteSize        = (mesh.VertexCount * mesh.VertexUnitStreamSize) + (mesh.IndexCount * mesh.IndexUnitStreamSize);

            /* Computing offset data */
            importer_data.VertexOffset += ai_mesh->mNumVertices;
            importer_data.IndexOffset += index_count;
        }
    }

    void AssimpImporter::ExtractMaterials(const aiScene* scene, ImporterData& importer_data)
    {
        if (!scene)
        {
            return;
        }

        uint32_t                  number_of_materials = scene->mNumMaterials;
        importer_data.Scene.Materials.reserve(number_of_materials);
        importer_data.Scene.MaterialNames.reserve(number_of_materials);

        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            REPORT_LOG(fmt::format("Extrating materials : {0}/{1}", (m + 1), number_of_materials).c_str())

            aiColor4D        color;
            aiMaterial*      ai_material = scene->mMaterials[m];
            aiString         mat_name    = ai_material->GetName();
            MeshMaterial&    material    = importer_data.Scene.Materials.emplace_back();

            importer_data.Scene.MaterialNames.push_back(mat_name.C_Str() ? std::string(mat_name.C_Str()) : std::string("<unamed material>"));

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS)
            {
                material.AmbientColor   = {color.r, color.g, color.b, color.a};
                material.AmbientColor.w = glm::min(material.AmbientColor.w, 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
            {
                material.DiffuseColor   = {color.r, color.g, color.b, color.a};
                material.DiffuseColor.w = std::min(material.DiffuseColor.w, 1.0f);
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS)
            {
                material.EmissiveColor   = {color.r, color.g, color.b, color.a};
                material.EmissiveColor.w = std::min(material.EmissiveColor.w, 1.0f);
            }

            float       opacity              = 1.0f;
            const float opaqueness_threshold = 0.05f;

            if (aiGetMaterialFloat(ai_material, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS)
            {
                material.TransparencyFactor = glm::clamp(1.f - opacity, 0.0f, 1.0f);
                if (material.TransparencyFactor >= (1.0f - opaqueness_threshold))
                {
                    material.TransparencyFactor = 0.0f;
                }
            }

            if (aiGetMaterialColor(ai_material, AI_MATKEY_COLOR_TRANSPARENT, &color) == AI_SUCCESS)
            {
                const float component_as_opacity = std::max(std::max(color.r, color.g), color.b);
                material.TransparencyFactor      = glm::clamp(component_as_opacity, 0.0f, 1.0f);
                if (material.TransparencyFactor >= (1.0f - opaqueness_threshold))
                {
                    material.TransparencyFactor = 0.0f;
                }
                material.AlphaTest = 0.5f;
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
        float            blend              = 1.0f;
        aiTextureOp      texture_operation  = aiTextureOp_Add;
        aiTextureMapMode texture_map_mode[] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        uint32_t         texture_flags      = 0;

        uint32_t number_of_materials = scene->mNumMaterials;
        for (uint32_t m = 0; m < number_of_materials; ++m)
        {
            REPORT_LOG(fmt::format("Extrating Material's textures:  {0}/{1} materials", (m + 1), number_of_materials).c_str())

            aiMaterial* ai_material = scene->mMaterials[m];

            if (aiGetMaterialTexture(
                    ai_material, aiTextureType_DIFFUSE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                AI_SUCCESS)
            {
                importer_data.Scene.Materials[m].AlbedoTextureMap = GenerateFileIndex(importer_data.Scene.Files, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(
                    ai_material, aiTextureType_EMISSIVE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                AI_SUCCESS)
            {
                importer_data.Scene.Materials[m].EmissiveTextureMap = GenerateFileIndex(importer_data.Scene.Files, texture_filename.C_Str());
            }

            if (aiGetMaterialTexture(
                    ai_material, aiTextureType_NORMALS, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                AI_SUCCESS)
            {
                importer_data.Scene.Materials[m].NormalTextureMap = GenerateFileIndex(importer_data.Scene.Files, texture_filename.C_Str());
            }

            if (importer_data.Scene.Materials[m].NormalTextureMap == 0xFFFFFFFF)
            {
                if (aiGetMaterialTexture(
                        ai_material, aiTextureType_HEIGHT, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                    AI_SUCCESS)
                {
                    importer_data.Scene.Materials[m].NormalTextureMap = GenerateFileIndex(importer_data.Scene.Files, texture_filename.C_Str());
                }
            }

            if (aiGetMaterialTexture(
                    ai_material, aiTextureType_OPACITY, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                AI_SUCCESS)
            {
                importer_data.Scene.Materials[m].OpacityTextureMap = GenerateFileIndex(importer_data.Scene.Files, texture_filename.C_Str());
                importer_data.Scene.Materials[m].AlphaTest   = 0.5f;
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

    void AssimpImporter::TraverseNode(const aiScene* ai_scene, ZEngine::Rendering::Scenes::SceneRawData* const scene, const aiNode* node, int parent_node_id, int depth_level)
    {
        auto node_id              = AddNode(scene, parent_node_id, depth_level);
        scene->NodeNames[node_id] = scene->Names.size();
        scene->Names.push_back(node->mName.C_Str() ? std::string(node->mName.C_Str()) : std::string{"<unamed node>"});

        scene->GlobalTransformCollection[node_id] = glm::mat4(1.0f);
        scene->LocalTransformCollection[node_id]  = ConvertToMat4(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            auto     sub_node_id = AddNode(scene, node_id, depth_level + 1);
            uint32_t mesh        = node->mMeshes[i];
            aiString mesh_name   = ai_scene->mMeshes[mesh]->mName;

            scene->NodeNames[sub_node_id] = scene->Names.size();
            scene->Names.push_back(mesh_name.C_Str() ? std::string(mesh_name.C_Str()) : std::string{"<unamed node>"});

            scene->NodeMeshes[sub_node_id]                = mesh;
            scene->NodeMaterials[sub_node_id]             = ai_scene->mMeshes[mesh]->mMaterialIndex;
            scene->GlobalTransformCollection[sub_node_id] = glm::mat4(1.0f);
            scene->LocalTransformCollection[sub_node_id]  = glm::mat4(1.0f);
        }

        for (uint32_t child = 0; child < node->mNumChildren; ++child)
        {
            TraverseNode(ai_scene, scene, node->mChildren[child], node_id, (depth_level + 1));
        }
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

    int AssimpImporter::AddNode(ZEngine::Rendering::Scenes::SceneRawData* scene, int parent, int depth)
    {
        int node_id = (int) scene->NodeHierarchyCollection.size();

        scene->NodeHierarchyCollection.push_back({.Parent = parent});
        scene->LocalTransformCollection.emplace_back(1.0f);
        scene->GlobalTransformCollection.emplace_back(1.0f);

        if (parent > -1)
        {
            int first_child = scene->NodeHierarchyCollection[parent].FirstChild;

            if (first_child == -1)
            {
                scene->NodeHierarchyCollection[parent].FirstChild = node_id;
            }
            else
            {
                int right_sibling = scene->NodeHierarchyCollection[first_child].RightSibling;
                if (right_sibling > -1)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; scene->NodeHierarchyCollection[right_sibling].RightSibling != -1;
                         right_sibling = scene->NodeHierarchyCollection[right_sibling].RightSibling)
                    {
                    }
                    scene->NodeHierarchyCollection[right_sibling].RightSibling = node_id;
                }
                else
                {
                    scene->NodeHierarchyCollection[first_child].RightSibling = node_id;
                }
            }
        }
        scene->NodeHierarchyCollection[node_id].DepthLevel   = depth;
        scene->NodeHierarchyCollection[node_id].RightSibling = -1;
        scene->NodeHierarchyCollection[node_id].FirstChild   = -1;

        return node_id;
    }

    int AssimpImporter::GenerateFileIndex(std::vector<std::string>& data, std::string_view filename)
    {
        auto find = std::find(std::begin(data), std::end(data), filename);
        if (find != std::end(data))
        {
            return std::distance(std::begin(data), find);
        }

        data.push_back(filename.data());
        return (data.size() - 1);
    }

    void AssimpProgressHandler::SetImporter(AssimpImporter* const importer)
    {
        m_importer = importer;
    }

    bool AssimpProgressHandler::Update(float percentage)
    {
        if (m_importer && m_importer->m_progress_callback)
        {
            m_importer->m_progress_callback(percentage);
        }
        return true;
    }
} // namespace Tetragrama::Importers
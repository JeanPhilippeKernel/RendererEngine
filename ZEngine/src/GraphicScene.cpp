#include <pch.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Components/TransformComponent.h>
#include <Rendering/Components/NameComponent.h>
#include <Rendering/Components/MaterialComponent.h>
#include <Rendering/Components/GeometryComponent.h>
#include <Rendering/Components/LightComponent.h>
#include <Rendering/Components/CameraComponent.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <Rendering/Materials/StandardMaterial.h>
#include <Rendering/Components/UUIComponent.h>
#include <Rendering/Components/ValidComponent.h>
#include <Rendering/Lights/DirectionalLight.h>

#include <Core/Coroutine.h>
#include <Rendering/Renderers/GraphicRenderer.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>
#include <Helpers/MathHelper.h>
#include <fmt/format.h>

#include <Rendering/Textures/Texture2D.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <stb/stb_image.h>
#include <stb/stb_image_resize.h>


#define SCENE_ROOT_PARENT_ID -1
#define SCENE_ROOT_DEPTH_LEVEL 0
#define INVALID_SCENE_NODE_ID -1
#define INVALID_TEXTURE_MAP 0xFFFFFFFF

using namespace ZEngine::Controllers;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace ZEngine::Rendering::Scenes
{
    std::recursive_mutex     GraphicScene::s_scene_node_mutex;
    std::vector<std::string> GraphicScene::s_texture_file_collection = {};
    Ref<SceneRawData>        GraphicScene::s_raw_data                = CreateRef<SceneRawData>();

    void GraphicScene::Initialize()
    {
        s_raw_data->EntityRegistry = std::make_shared<entt::registry>();
    }

    void GraphicScene::Deinitialize()
    {
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);

        auto scene_node_identifier = co_await AddNodeAsync(SCENE_ROOT_PARENT_ID, SCENE_ROOT_DEPTH_LEVEL);
        co_await SetSceneNodeNameAsync(scene_node_identifier, entity_name);

        auto  scene_entity   = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[scene_node_identifier]);
        auto& name_component = scene_entity.GetComponent<NameComponent>();
        name_component.Name  = entity_name;
        co_return scene_entity;
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);
        auto             scene_entity = co_await CreateEntityAsync(entity_name);
        scene_entity.AddComponent<UUIComponent>(uuid);
        co_return scene_entity;
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);
        auto             scene_entity = co_await CreateEntityAsync(entity_name);
        scene_entity.AddComponent<UUIComponent>(uuid_string);
        co_return scene_entity;
    }

    std::future<Entities::GraphicSceneEntity> GraphicScene::GetEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);

        entt::entity entity_handle{entt::null};
        auto         views = s_raw_data->EntityRegistry->view<NameComponent>();
        for (auto entity : views)
        {
            auto name = views.get<NameComponent>(entity).Name;
            if (name == entity_name)
            {
                entity_handle = entity;
                break;
            }
        }

        if (entity_handle == entt::null)
        {
            ZENGINE_CORE_ERROR("An entity with name {0} deosn't exist", entity_name.data())
        }

        co_return GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, entity_handle);
    }

    std::future<bool> GraphicScene::RemoveEntityAsync(const Entities::GraphicSceneEntity& entity)
    {
        std::unique_lock lock(s_scene_node_mutex);
        if (!s_raw_data->EntityRegistry->valid(entity))
        {
            ZENGINE_CORE_ERROR("This entity is no longer valid")
            co_return false;
        }
        s_raw_data->EntityRegistry->destroy(entity);
        co_return true;
    }

    std::shared_ptr<entt::registry> GraphicScene::GetRegistry()
    {
        return s_raw_data->EntityRegistry;
    }

    std::future<int32_t> GraphicScene::AddNodeAsync(int parent_node_id, int depth_level)
    {
        std::unique_lock lock(s_scene_node_mutex);

        int scene_node_identifier = (int) s_raw_data->NodeHierarchyCollection.size();

        s_raw_data->NodeHierarchyCollection.push_back({.Parent = parent_node_id});
        s_raw_data->LocalTransformCollection.emplace_back(1.0f);
        s_raw_data->GlobalTransformCollection.emplace_back(1.0f);

        if (parent_node_id > SCENE_ROOT_PARENT_ID)
        {
            int first_child = s_raw_data->NodeHierarchyCollection[parent_node_id].FirstChild;

            if (first_child == INVALID_SCENE_NODE_ID)
            {
                s_raw_data->NodeHierarchyCollection[parent_node_id].FirstChild = scene_node_identifier;
            }
            else
            {
                int right_sibling = s_raw_data->NodeHierarchyCollection[first_child].RightSibling;
                if (right_sibling > INVALID_SCENE_NODE_ID)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; s_raw_data->NodeHierarchyCollection[right_sibling].RightSibling != INVALID_SCENE_NODE_ID;
                         right_sibling = s_raw_data->NodeHierarchyCollection[right_sibling].RightSibling)
                    {
                    }
                    s_raw_data->NodeHierarchyCollection[right_sibling].RightSibling = scene_node_identifier;
                }
                else
                {
                    s_raw_data->NodeHierarchyCollection[first_child].RightSibling = scene_node_identifier;
                }
            }
        }
        s_raw_data->NodeHierarchyCollection[scene_node_identifier].DepthLevel   = depth_level;
        s_raw_data->NodeHierarchyCollection[scene_node_identifier].RightSibling = INVALID_SCENE_NODE_ID;
        s_raw_data->NodeHierarchyCollection[scene_node_identifier].FirstChild   = INVALID_SCENE_NODE_ID;
        /*
         * Extra SceneEntity information for a SceneNode
         */
        s_raw_data->SceneNodeEntityMap[scene_node_identifier] = s_raw_data->EntityRegistry->create();
        //auto entity_wrapper = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[scene_node_identifier]);
        //entity_wrapper.AddComponent<TransformComponent>(s_raw_data->LocalTransformCollection[scene_node_identifier]);
        //entity_wrapper.AddComponent<UUIComponent>();
        //entity_wrapper.AddComponent<NameComponent>();

        co_return scene_node_identifier;
    }

    std::future<bool> GraphicScene::RemoveNodeAsync(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);

        return std::future<bool>();
    }

    int32_t GraphicScene::GetSceneNodeParent(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].Parent;
    }

    int32_t GraphicScene::GetSceneNodeFirstChild(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].FirstChild;
    }

    std::vector<int32_t> GraphicScene::GetSceneNodeSiblingCollection(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);

        std::vector<int32_t> sibling_scene_nodes = {};
        if (node_identifier < 0)
        {
            return sibling_scene_nodes;
        }

        for (auto sibling = s_raw_data->NodeHierarchyCollection[node_identifier].RightSibling; sibling != INVALID_SCENE_NODE_ID;
             sibling      = s_raw_data->NodeHierarchyCollection[sibling].RightSibling)
        {
            sibling_scene_nodes.push_back(sibling);
        }

        return sibling_scene_nodes;
    }

    std::string_view GraphicScene::GetSceneNodeName(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        return s_raw_data->SceneNodeNameMap.contains(node_identifier) ? s_raw_data->SceneNodeNameMap[node_identifier] : std::string_view();
    }

    glm::mat4& GraphicScene::GetSceneNodeLocalTransform(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT((node_identifier > INVALID_SCENE_NODE_ID) && (node_identifier < s_raw_data->LocalTransformCollection.size()), "node identifier is invalid")
        return s_raw_data->LocalTransformCollection[node_identifier];
    }

    glm::mat4& GraphicScene::GetSceneNodeGlobalTransform(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < s_raw_data->GlobalTransformCollection.size(), "node identifier is invalid")
        return s_raw_data->GlobalTransformCollection[node_identifier];
    }

    const SceneNodeHierarchy& GraphicScene::GetSceneNodeHierarchy(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < s_raw_data->NodeHierarchyCollection.size(), "node identifier is invalid")
        return s_raw_data->NodeHierarchyCollection[node_identifier];
    }

    GraphicSceneEntity GraphicScene::GetSceneNodeEntityWrapper(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(s_raw_data->SceneNodeEntityMap.contains(node_identifier), "node identifier is invalid")
        return GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[node_identifier]);
    }

    std::future<void> GraphicScene::SetSceneNodeNameAsync(int32_t node_identifier, std::string_view node_name)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID, "node identifier is invalid")
        s_raw_data->SceneNodeNameMap[node_identifier] = node_name;
        co_return;
    }

    std::future<Meshes::MeshVNext> GraphicScene::GetSceneNodeMeshAsync(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(s_raw_data->SceneNodeMeshMap.contains(node_identifier), "node identifier is invalid")
        co_return s_raw_data->SceneNodeMeshMap[node_identifier];
    }

    std::future<void> GraphicScene::ImportAssetAsync(std::string_view asset_filename)
    {
        std::unique_lock lock(s_scene_node_mutex);

        if (!asset_filename.empty())
        {
            __ReadAssetFileAsync(asset_filename, [](bool success, const void* scene, std::string_view material_texture_parent_path) -> std::future<void> {
                if (success && scene)
                {
                    auto    scene_ptr         = reinterpret_cast<const aiScene*>(scene);
                    aiNode* root_node         = scene_ptr->mRootNode;
                    bool    traverse_complete = co_await __TraverseAssetNodeAsync(scene_ptr, root_node, SCENE_ROOT_PARENT_ID, SCENE_ROOT_DEPTH_LEVEL, material_texture_parent_path);

                    if (traverse_complete)
                    {
                        /*
                         * Post-processing Material data
                         */
                        PostProcessMaterials();
                    }
                }
            });
        }
        co_return;
    }

    Ref<SceneRawData> GraphicScene::GetRawData()
    {
        std::lock_guard lock(s_scene_node_mutex);
        return s_raw_data;
    }

    bool GraphicScene::HasSceneNodes()
    {
        std::unique_lock lock(s_scene_node_mutex);
        return !s_raw_data->NodeHierarchyCollection.empty();
    }

    std::vector<int32_t> GraphicScene::GetRootSceneNodes()
    {
        std::unique_lock     lock(s_scene_node_mutex);
        std::vector<int32_t> root_scene_nodes;

        if (!HasSceneNodes())
        {
            return root_scene_nodes;
        }

        for (uint32_t i = 0; i < s_raw_data->NodeHierarchyCollection.size(); ++i)
        {
            if (s_raw_data->NodeHierarchyCollection[i].Parent == SCENE_ROOT_PARENT_ID)
            {
                root_scene_nodes.push_back(i);
            }
        }
        return root_scene_nodes;
    }

    void GraphicScene::ComputeAllTransforms()
    {
        std::unique_lock lock(s_scene_node_mutex);
        uint32_t         total_level = s_raw_data->LevelSceneNodeChangedMap.size();
        for (int level = 0; level < total_level; ++level)
        {
            if (!s_raw_data->LevelSceneNodeChangedMap[level].empty())
            {
                auto& changed_scene_node_collection = s_raw_data->LevelSceneNodeChangedMap[level];
                for (const int& node_identifier : changed_scene_node_collection)
                {
                    if (node_identifier != SCENE_ROOT_PARENT_ID)
                    {
                        int parent = s_raw_data->NodeHierarchyCollection[node_identifier].Parent;
                        if (parent != SCENE_ROOT_PARENT_ID)
                        {
                            s_raw_data->GlobalTransformCollection[node_identifier] =
                                s_raw_data->GlobalTransformCollection[parent] * s_raw_data->LocalTransformCollection[node_identifier];
                        }
                    }
                }
                s_raw_data->LevelSceneNodeChangedMap[level].clear();
            }
        }
    }

    void GraphicScene::MarkSceneNodeAsChanged(int32_t node_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);

        if (s_raw_data->NodeHierarchyCollection.empty())
        {
            return;
        }

        auto node_level = s_raw_data->NodeHierarchyCollection[node_identifier].DepthLevel;
        s_raw_data->LevelSceneNodeChangedMap[node_level].emplace(node_identifier);

        auto first_child = s_raw_data->NodeHierarchyCollection[node_identifier].FirstChild;
        if (first_child != INVALID_SCENE_NODE_ID)
        {
            auto sibling_collection = GetSceneNodeSiblingCollection(first_child);
            s_raw_data->LevelSceneNodeChangedMap[node_level].emplace(first_child);
            for (auto sibling : sibling_collection)
            {
                s_raw_data->LevelSceneNodeChangedMap[node_level].emplace(sibling);
            }
        }
    }

    std::future<bool> GraphicScene::__TraverseAssetNodeAsync(
        const aiScene*   assimp_scene,
        aiNode*          node,
        int              parent_node,
        int              depth_level,
        std::string_view material_texture_parent_path)
    {
        std::unique_lock lock(s_scene_node_mutex);

        bool result = true;
        if (!assimp_scene && !node)
        {
            result = false;
            co_return result;
        }

        auto scene_node_identifier                                  = co_await AddNodeAsync(parent_node, depth_level);
        s_raw_data->SceneNodeNameMap[scene_node_identifier]         = node->mName.C_Str() ? std::string(node->mName.C_Str()) : std::string{"<unamed node>"};
        s_raw_data->LocalTransformCollection[scene_node_identifier] = Helpers::ConvertToMat4(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const SceneNodeHierarchy& hierarchy   = s_raw_data->NodeHierarchyCollection[scene_node_identifier];
            int32_t                   sub_node_id = co_await AddNodeAsync(scene_node_identifier, hierarchy.DepthLevel + 1);
            /*
             * Processing Mesh data
             */
            uint32_t mesh_id   = node->mMeshes[i];
            aiString mesh_name = assimp_scene->mMeshes[mesh_id]->mName;
            s_raw_data->SceneNodeNameMap[sub_node_id] =
                mesh_name.C_Str() ? std::string(mesh_name.C_Str()) : fmt::format("{0}_Mesh_{1}", s_raw_data->SceneNodeNameMap[scene_node_identifier].c_str(), i);
            s_raw_data->SceneNodeMeshMap[sub_node_id] = co_await __ReadSceneNodeMeshDataAsync(assimp_scene, mesh_id);
            /*
             * Processing Material data
             */
            uint32_t material_id                              = assimp_scene->mMeshes[mesh_id]->mMaterialIndex;
            aiString material_name                            = assimp_scene->mMaterials[material_id]->GetName();
            s_raw_data->SceneNodeMaterialNameMap[sub_node_id] = material_name.C_Str() ? std::string(material_name.C_Str()) : std::string{};
            s_raw_data->SceneNodeMaterialMap[sub_node_id]     = co_await __ReadSceneNodeMeshMaterialDataAsync(assimp_scene, material_id, material_texture_parent_path);
        }

        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            result = co_await __TraverseAssetNodeAsync(assimp_scene, node->mChildren[i], scene_node_identifier, depth_level + 1, material_texture_parent_path);
            if (!result)
            {
                break;
            }
        }
        co_return result;
    }

    std::future<Meshes::MeshMaterial> GraphicScene::__ReadSceneNodeMeshMaterialDataAsync(
        const aiScene*   assimp_scene,
        uint32_t         material_identifier,
        std::string_view material_texture_parent_path)
    {
        std::unique_lock lock(s_scene_node_mutex);

        Meshes::MeshMaterial output_material = {};

        aiMaterial* assimp_material = assimp_scene->mMaterials[material_identifier];
        aiColor4D   ai_color;

        if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_AMBIENT, &ai_color) == AI_SUCCESS)
        {
            output_material.AmbientColor   = {ai_color.r, ai_color.g, ai_color.b, ai_color.a};
            output_material.AmbientColor.w = std::min(output_material.AmbientColor.w, 1.0f);
            output_material.EmissiveColor  = output_material.AmbientColor;
        }

        if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_DIFFUSE, &ai_color) == AI_SUCCESS)
        {
            output_material.DiffuseColor   = {ai_color.r, ai_color.g, ai_color.b, ai_color.a};
            output_material.DiffuseColor.w = std::min(output_material.DiffuseColor.w, 1.0f);
            output_material.AlbedoColor    = output_material.DiffuseColor;
        }

        if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_EMISSIVE, &ai_color) == AI_SUCCESS)
        {
            output_material.EmissiveColor.x += ai_color.r;
            output_material.EmissiveColor.y += ai_color.g;
            output_material.EmissiveColor.z += ai_color.b;
            output_material.EmissiveColor.w += ai_color.a;
            output_material.EmissiveColor.w = std::min(output_material.EmissiveColor.w, 1.0f);
        }

        float       opacity              = 1.0f;
        const float opaqueness_threshold = 0.05f;

        if (aiGetMaterialFloat(assimp_material, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS)
        {
            output_material.TransparencyFactor = glm::clamp(1.f - opacity, 0.0f, 1.0f);
            if (output_material.TransparencyFactor >= (1.0f - opaqueness_threshold))
            {
                output_material.TransparencyFactor = 0.0f;
            }
        }

        if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_TRANSPARENT, &ai_color) == AI_SUCCESS)
        {
            const float component_as_opacity   = glm::max(glm::max(ai_color.r, ai_color.g), ai_color.b);
            output_material.TransparencyFactor = glm::clamp(component_as_opacity, 0.0f, 1.0f);
            if (output_material.TransparencyFactor >= (1.0f - opaqueness_threshold))
            {
                output_material.TransparencyFactor = 0.0f;
            }
            output_material.AlphaTest = 0.5f;
        }
        /*
         * PBR properties
         */
        float material_ai_value;
        if (aiGetMaterialFloat(assimp_material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, &material_ai_value) == AI_SUCCESS)
        {
            output_material.MetallicFactor = material_ai_value;
        }

        if (aiGetMaterialFloat(assimp_material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, &material_ai_value) == AI_SUCCESS)
        {
            output_material.RoughnessColor = {material_ai_value, material_ai_value, material_ai_value, material_ai_value};
        }
        /*
         * Texture files
         */
        aiString              texture_filename;
        aiTextureMapping      texture_mapping;
        uint32_t              uv_index;
        float                 blend                 = 1.0f;
        aiTextureOp           texture_operation     = aiTextureOp_Add;
        aiTextureMapMode      texture_map_mode[]    = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        uint32_t              texture_flags         = 0;
        std::string_view      texture_dir_fomart    = "{0}\\{1}";

        if (aiGetMaterialTexture(
                assimp_material, aiTextureType_EMISSIVE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
            AI_SUCCESS)
        {
            auto filename                      = fmt::format(texture_dir_fomart, material_texture_parent_path, texture_filename.C_Str());
            output_material.EmissiveTextureMap = AddTexture(filename);
        }

        if (aiGetMaterialTexture(
                assimp_material, aiTextureType_DIFFUSE, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
            AI_SUCCESS)
        {
            auto filename                    = fmt::format(texture_dir_fomart, material_texture_parent_path, texture_filename.C_Str());
            output_material.AlbedoTextureMap = AddTexture(filename);
        }

        if (aiGetMaterialTexture(
                assimp_material, aiTextureType_NORMALS, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
            AI_SUCCESS)
        {
            auto filename                    = fmt::format(texture_dir_fomart, material_texture_parent_path, texture_filename.C_Str());
            output_material.NormalTextureMap = AddTexture(filename);
        }

        if (output_material.NormalTextureMap == INVALID_TEXTURE_MAP)
        {
            if (aiGetMaterialTexture(
                    assimp_material, aiTextureType_HEIGHT, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
                AI_SUCCESS)
            {
                auto filename                    = fmt::format(texture_dir_fomart, material_texture_parent_path, texture_filename.C_Str());
                output_material.NormalTextureMap = AddTexture(filename);
            }
        }

        if (aiGetMaterialTexture(
                assimp_material, aiTextureType_OPACITY, 0, &texture_filename, &texture_mapping, &uv_index, &blend, &texture_operation, texture_map_mode, &texture_flags) ==
            AI_SUCCESS)
        {
            auto filename                     = fmt::format(texture_dir_fomart, material_texture_parent_path, texture_filename.C_Str());
            output_material.OpacityTextureMap = AddTexture(filename);
            output_material.AlphaTest         = 0.5f;
        }
        co_return output_material;
    }

    std::future<Meshes::MeshVNext> GraphicScene::__ReadSceneNodeMeshDataAsync(const aiScene* assimp_scene, uint32_t mesh_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);

        aiMesh* assimp_mesh = assimp_scene->mMeshes[mesh_identifier];

        uint32_t               vertex_count{0};
        std::vector<float>&    vertices = s_raw_data->Vertices;
        std::vector<uint32_t>& indices  = s_raw_data->Indices;

        /* Vertice processing */
        for (int i = 0; i < assimp_mesh->mNumVertices; ++i)
        {
            const aiVector3D position = assimp_mesh->mVertices[i];
            vertices.push_back(position.x);
            vertices.push_back(position.y);
            vertices.push_back(position.z);

            const aiVector3D normal = assimp_mesh->mNormals[i];
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);

            if (assimp_mesh->HasTextureCoords(0))
            {
                const aiVector3D tex_coord = assimp_mesh->mTextureCoords[0][i];
                vertices.push_back(tex_coord.x);
                vertices.push_back(tex_coord.y);
            }
            else
            {
                vertices.push_back(0.0);
                vertices.push_back(0.0);
            }

            vertex_count++;
        }

        /* Face and Indices processing */
        uint32_t index_count{0};
        for (int x = 0; x < assimp_mesh->mNumFaces; ++x)
        {
            aiFace assimp_mesh_face = assimp_mesh->mFaces[x];

            for (int y = 0; y < assimp_mesh_face.mNumIndices; ++y)
            {
                indices.push_back(assimp_mesh_face.mIndices[y]);

                index_count++;
            }
        }

        Meshes::MeshVNext mesh    = {};
        mesh.VertexCount          = vertex_count;
        mesh.VertexOffset         = s_raw_data->SVertexOffset;
        mesh.VertexUnitStreamSize = sizeof(Renderers::Storages::IVertex);
        mesh.StreamOffset         = (mesh.VertexUnitStreamSize * mesh.VertexOffset);
        mesh.IndexOffset          = s_raw_data->SIndexOffset;
        mesh.IndexCount           = index_count;
        mesh.IndexUnitStreamSize  = sizeof(uint32_t);
        mesh.IndexStreamOffset    = (mesh.IndexUnitStreamSize * mesh.IndexOffset);
        mesh.TotalByteSize        = (mesh.VertexCount * mesh.VertexUnitStreamSize) + (mesh.IndexCount * mesh.IndexUnitStreamSize);

        s_raw_data->SVertexOffset += assimp_mesh->mNumVertices;
        s_raw_data->SIndexOffset += index_count;

        co_return mesh;
    }

    int32_t GraphicScene::AddTexture(std::string_view filename)
    {
        std::unique_lock lock(s_scene_node_mutex);

        if (filename.empty())
        {
            return -1;
        }

        if (!std::filesystem::exists(filename))
        {
            return -1;
        }

        auto found = std::find(s_texture_file_collection.begin(), s_texture_file_collection.end(), std::string(filename));
        if (found == std::end(s_texture_file_collection))
        {
            //s_raw_data->TextureCollection->Add(Textures::Texture2D::Read(filename));

            s_texture_file_collection.emplace_back(filename.data());
            return (s_texture_file_collection.size() - 1);
        }

        return std::distance(std::begin(s_texture_file_collection), found);
    }

    void GraphicScene::PostProcessMaterials()
    {
        std::unique_lock lock(s_scene_node_mutex);

        /*
        * Ensuring output directory exist
        */
        const auto current_directoy = std::filesystem::current_path();
        auto output_texture_dir = fmt::format("{0}\\{1}", current_directoy.string(), "__imported/out_textures/");

        std::map<std::string_view, uint32_t> opacity_map_indices = {};
        auto& material_map = s_raw_data->SceneNodeMaterialMap;
        for (auto& material : material_map)
        {
            auto& material_data = material.second;
            if ((material_data.OpacityTextureMap != INVALID_TEXTURE_MAP) && (material_data.AlbedoTextureMap != INVALID_TEXTURE_MAP))
            {
                opacity_map_indices[s_texture_file_collection[material_data.AlbedoTextureMap]] = material_data.OpacityTextureMap;
            }
        }

        for (auto& file : s_texture_file_collection)
        {
            /*
             * Downscaling textures
             */
            const int            max_width   = 512;
            const int            max_height  = 512;
            const uint32_t       max_channel = 4;
            std::vector<uint8_t> temp_buffer(max_width * max_height * max_channel);

            uint8_t* downscaled_texture_pixel = nullptr;

            auto filename = std::filesystem::path(file).filename();
            auto downscaled_texture = fmt::format("{0}{1}__rescaled.png", output_texture_dir, filename.string());

            int      width, height, channel;
            stbi_uc* file_pixel      = stbi_load(file.data(), &width, &height, &channel, STBI_rgb_alpha);
            downscaled_texture_pixel = file_pixel;
            channel                  = STBI_rgb_alpha; /*force channel to be RGBA*/

            if (!downscaled_texture_pixel)
            {
                width                    = max_width;
                height                   = max_height;
                downscaled_texture_pixel = temp_buffer.data();
            }

            /*handling opacity combinaison with albedo*/
            if (opacity_map_indices.contains(file))
            {
                int      opacity_width, opacity_height;
                auto     opacity_file  = s_texture_file_collection[opacity_map_indices[file]];
                stbi_uc* opacity_pixel = stbi_load(opacity_file.data(), &opacity_width, &opacity_height, nullptr, 1);

                if (!opacity_pixel)
                {
                    ZENGINE_CORE_ERROR("failed to load opacity file {0}", opacity_file.data())
                    return;
                }

                ZENGINE_VALIDATE_ASSERT(opacity_pixel, "")
                ZENGINE_VALIDATE_ASSERT(opacity_width == width, "")
                ZENGINE_VALIDATE_ASSERT(opacity_height == height, "")

                for (int y = 0; y < opacity_height; y++)
                {
                    for (int x = 0; x < opacity_width; x++)
                    {
                        downscaled_texture_pixel[(y * opacity_width + x) * channel + 3] = opacity_pixel[y * opacity_width + x];
                    }
                }

                stbi_image_free(opacity_pixel);
            }

            /*
             * Writing out downscaled texture
             */
            const uint32_t       output_size = width * height * channel;
            std::vector<uint8_t> mip_buffer(output_size);
            uint32_t             output_width  = std::min(width, max_width);
            uint32_t             output_height = std::min(height, max_height);

            stbir_resize_uint8(downscaled_texture_pixel, width, height, 0, mip_buffer.data(), output_width, output_height, 0, channel);
            stbi_write_png(downscaled_texture.c_str(), output_width, output_height, channel, mip_buffer.data(), 0);

            /*
            * Override filename
            */
            file = downscaled_texture;

            if (file_pixel)
            {
                stbi_image_free(file_pixel);
            }
        }

        for (std::string_view file : s_texture_file_collection)
        {
            auto index = Renderers::GraphicRenderer::GlobalTextures->Add(Textures::Texture2D::Read(file));
            s_raw_data->TextureCollection.emplace(index);
        }
    }

    void GraphicScene::__ReadAssetFileAsync(std::string_view filename, ReadCallback callback)
    {
        std::thread([path = std::string(filename.data()), callback] {
            std::filesystem::path asset_path(path);
            auto                  parent_directory = asset_path.parent_path();

            Assimp::Importer importer = {};
            uint32_t         read_flags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_SplitLargeMeshes |
                                  aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials | aiProcess_GenUVCoords | aiProcess_FlipUVs |
                                  aiProcess_ValidateDataStructure | aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_LimitBoneWeights;

            bool           result    = true;
            const aiScene* scene_ptr = importer.ReadFile(path, read_flags);
            if ((!scene_ptr) || scene_ptr->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene_ptr->mRootNode)
            {
                result = false;
            }

            if (callback)
            {
                callback(result, scene_ptr, parent_directory.string());
            }
            importer.FreeScene();
        }).detach();
    }

    GraphicSceneEntity GraphicScene::GetPrimariyCameraEntity()
    {
        GraphicSceneEntity camera_entity;

        auto view_cameras = s_raw_data->EntityRegistry->view<CameraComponent>();
        for (auto entity : view_cameras)
        {
            auto& component = view_cameras.get<CameraComponent>(entity);
            if (component.IsPrimaryCamera)
            {
                camera_entity = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, entity);
                break;
            }
        }
        return camera_entity;
    }
} // namespace ZEngine::Rendering::Scenes

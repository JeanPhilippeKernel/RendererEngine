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

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <Helpers/MathHelper.h>
#include <fmt/format.h>


#define SCENE_ROOT_PARENT_ID -1
#define SCENE_ROOT_DEPTH_LEVEL 0
#define INVALID_SCENE_NODE_ID -1

using namespace ZEngine::Controllers;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace ZEngine::Rendering::Scenes
{
    std::recursive_mutex                          GraphicScene::s_scene_node_mutex;
    Ref<SceneRawData>                             GraphicScene::s_raw_data = CreateRef<SceneRawData>();

    void GraphicScene::Initialize()
    {
        s_raw_data->EntityRegistry = CreateRef<entt::registry>();
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
            ZENGINE_CORE_ERROR("An entity with name {0} deosn't exist", entity_name)
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

    Ref<entt::registry> GraphicScene::GetRegistry()
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
        auto entity_wrapper = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[scene_node_identifier]);
        entity_wrapper.AddComponent<TransformComponent>(s_raw_data->LocalTransformCollection[scene_node_identifier]);
        entity_wrapper.AddComponent<UUIComponent>();
        entity_wrapper.AddComponent<NameComponent>();

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
            __ReadAssetFileAsync(asset_filename, [](bool success, const void* scene) -> std::future<void> {
                if (success && scene)
                {
                    auto    scene_ptr = reinterpret_cast<const aiScene*>(scene);
                    aiNode* root_node = scene_ptr->mRootNode;
                    co_await __TraverseAssetNodeAsync(scene_ptr, root_node, SCENE_ROOT_PARENT_ID, SCENE_ROOT_DEPTH_LEVEL);
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
        uint32_t total_level = s_raw_data->LevelSceneNodeChangedMap.size();
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

    std::future<bool> GraphicScene::__TraverseAssetNodeAsync(const aiScene* assimp_scene, aiNode* node, int parent_node, int depth_level)
    {
        std::unique_lock lock(s_scene_node_mutex);

        bool result = true;
        if (!assimp_scene && !node)
        {
            result = false;
            co_return result;
        }

        auto scene_node_identifier                          = co_await AddNodeAsync(parent_node, depth_level);
        s_raw_data->SceneNodeNameMap[scene_node_identifier] = node->mName.C_Str() ? std::string(node->mName.C_Str()) : std::string{"<unamed node>"};

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const SceneNodeHierarchy& hierarchy   = s_raw_data->NodeHierarchyCollection[scene_node_identifier];
            int32_t              sub_node_id = co_await AddNodeAsync(scene_node_identifier, hierarchy.DepthLevel + 1);
            /*
             * Processing Mesh data
             */
            uint32_t mesh_id                          = node->mMeshes[i];
            aiString mesh_name                        = assimp_scene->mMeshes[mesh_id]->mName;
            s_raw_data->SceneNodeNameMap[sub_node_id] =
                mesh_name.C_Str() ? std::string(mesh_name.C_Str()) : fmt::format("{0}_Mesh_{1}", s_raw_data->SceneNodeNameMap[scene_node_identifier].c_str(), i);
            s_raw_data->SceneNodeMeshMap[sub_node_id] = co_await __ReadSceneNodeMeshDataAsync(assimp_scene, mesh_id);
            /*
             * Processing Material data
             */
            uint32_t material_id                              = assimp_scene->mMeshes[mesh_id]->mMaterialIndex;
            aiString material_name                            = assimp_scene->mMaterials[material_id]->GetName();
            s_raw_data->SceneNodeMaterialMap[sub_node_id]     = material_id;
            s_raw_data->SceneNodeMaterialNameMap[sub_node_id] = material_name.C_Str() ? std::string(material_name.C_Str()) : std::string{};
        }
        s_raw_data->LocalTransformCollection[scene_node_identifier] = Helpers::ConvertToMat4(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            result = co_await __TraverseAssetNodeAsync(assimp_scene, node->mChildren[i], scene_node_identifier, depth_level + 1);
            if (!result)
            {
                break;
            }
        }
        co_return result;
    }

    std::future<Meshes::MeshVNext> GraphicScene::__ReadSceneNodeMeshDataAsync(const aiScene* assimp_scene, uint32_t mesh_identifier)
    {
        std::unique_lock lock(s_scene_node_mutex);

        aiMesh* assimp_mesh = assimp_scene->mMeshes[mesh_identifier];

        uint32_t               vertex_count{0};
        std::vector<float>&    vertices = s_raw_data->Vertices;
        std::vector<uint32_t>& indices  = s_raw_data->Indices;

        /* Vertice processing */
        for (int x = 0; x < assimp_mesh->mNumVertices; ++x)
        {
            vertices.push_back(assimp_mesh->mVertices[x].x);
            vertices.push_back(assimp_mesh->mVertices[x].y);
            vertices.push_back(assimp_mesh->mVertices[x].z);

            vertices.push_back(assimp_mesh->mNormals[x].x);
            vertices.push_back(assimp_mesh->mNormals[x].y);
            vertices.push_back(assimp_mesh->mNormals[x].z);

            if (assimp_mesh->mTextureCoords[0])
            {
                vertices.push_back(assimp_mesh->mTextureCoords[0][x].x);
                vertices.push_back(assimp_mesh->mTextureCoords[0][x].y);
            }
            else
            {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
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

    void GraphicScene::__ReadAssetFileAsync(std::string_view filename, ReadCallback callback)
    {
        std::thread([fn = std::string(filename.data()), callback] {
            Assimp::Importer importer = {};
            uint32_t read_flags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights | aiProcess_SplitLargeMeshes |
                                  aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials | aiProcess_FindDegenerates | aiProcess_FindInvalidData |
                                  aiProcess_GenUVCoords | aiProcess_FlipUVs;

            bool           result    = true;
            const aiScene* scene_ptr = importer.ReadFile(fn, read_flags);
            if ((!scene_ptr) || scene_ptr->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene_ptr->mRootNode)
            {
                result = false;
            }

            if (callback)
            {
                callback(result, scene_ptr);
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

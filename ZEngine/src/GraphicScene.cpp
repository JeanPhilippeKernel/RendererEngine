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
#include <Helpers/ThreadPool.h>
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
    std::recursive_mutex GraphicScene::s_scene_node_mutex;
    Ref<SceneRawData>    GraphicScene::s_raw_data = CreateRef<SceneRawData>();

    void GraphicScene::Initialize()
    {
        std::string_view root_node = "Root";
        s_raw_data->NodeNames[0]   = 0;
        s_raw_data->Names.emplace_back(root_node);
        s_raw_data->GlobalTransformCollection.emplace_back(1.0f);
        s_raw_data->LocalTransformCollection.emplace_back(1.0f);
        s_raw_data->NodeHierarchyCollection.push_back({.Parent = -1, .FirstChild = -1, .DepthLevel = 0});

        s_raw_data->EntityRegistry = std::make_shared<entt::registry>();
    }

    void GraphicScene::Deinitialize() {}
    void GraphicScene::SetRootNodeName(std::string_view name)
    {
        {
            std::lock_guard l(s_scene_node_mutex);
            if (s_raw_data && (!s_raw_data->Names.empty()))
            {
                s_raw_data->Names[0] = name;
            }
        }
    }

    void GraphicScene::Merge(std::span<SceneRawData> scenes)
    {
        {
            std::lock_guard l(s_scene_node_mutex);

            if (!scenes.empty())
            {
                s_raw_data->NodeHierarchyCollection[0].FirstChild = 1;
            }

            MergeScenes(scenes);
            MergeMeshData(scenes);
            MergeMaterials(scenes);


            for (std::string_view file : s_raw_data->Files)
            {
                auto index = Renderers::GraphicRenderer::GlobalTextures->Add(Textures::Texture2D::Read(file));
                s_raw_data->TextureCollection.emplace(index);
            }
        }
    }

    void GraphicScene::MergeScenes(std::span<SceneRawData> scenes)
    {
        auto& hierarchy        = s_raw_data->NodeHierarchyCollection;
        auto& global_transform = s_raw_data->GlobalTransformCollection;
        auto& local_transform  = s_raw_data->LocalTransformCollection;
        auto& names            = s_raw_data->Names;
        auto& materialNames    = s_raw_data->MaterialNames;

        int offs            = 1;
        int mesh_offset     = 0;
        int material_offset = 0;
        int name_offset     = (int) names.size();

        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.NodeHierarchyCollection}, hierarchy);
            MergeVector(std::span{scene.LocalTransformCollection}, local_transform);
            MergeVector(std::span{scene.GlobalTransformCollection}, global_transform);

            MergeVector(std::span{scene.Names}, names);
            MergeVector(std::span{scene.MaterialNames}, materialNames);

            int node_count = scene.NodeHierarchyCollection.size();

            // Shifting node index
            for (int i = 0; i < node_count; ++i)
            {
                auto& h = hierarchy[i + offs];
                if (h.Parent > -1)
                {
                    h.Parent += offs;
                }
                if (h.FirstChild > -1)
                {
                    h.FirstChild += offs;
                }
                if (h.RightSibling > -1)
                {
                    h.RightSibling += offs;
                }
            }

            MergeMap(scene.NodeMeshes, s_raw_data->NodeMeshes, offs, mesh_offset);
            MergeMap(scene.NodeNames, s_raw_data->NodeNames, offs, name_offset);
            MergeMap(scene.NodeMaterials, s_raw_data->NodeMaterials, offs, material_offset);

            offs += node_count;

            material_offset += scene.MaterialNames.size();
            name_offset += scene.Names.size();
            mesh_offset += scene.Meshes.size();
        }

        offs    = 1;
        int idx = 0;
        for (auto& scene : scenes)
        {
            int  nodeCount = (int) scene.NodeHierarchyCollection.size();
            bool isLast    = (idx == scenes.size() - 1);
            // calculate new next sibling for the old scene roots
            int next                     = isLast ? -1 : offs + nodeCount;
            hierarchy[offs].RightSibling = next;
            // attach to new root
            hierarchy[offs].Parent = 0;

            offs += nodeCount;
            idx++;
        }

        for (int i = 1; i < hierarchy.size(); ++i)
        {
            hierarchy[i].DepthLevel++;
        }
    }

    void GraphicScene::MergeMeshData(std::span<SceneRawData> scenes)
    {
        uint32_t totalVertexDataSize = 0;
        uint32_t totalIndexDataSize  = 0;

        uint32_t offs = 0;

        auto& vertices = s_raw_data->Vertices;
        auto& indices  = s_raw_data->Indices;
        auto& meshes   = s_raw_data->Meshes;

        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.Vertices}, vertices);
            MergeVector(std::span{scene.Indices}, indices);
            MergeVector(std::span{scene.Meshes}, meshes);

            uint32_t vtxOffset = totalVertexDataSize / 8; /* 8 is the number of per-vertex attributes: position, normal + UV */

            for (size_t j = 0; j < (uint32_t) scene.Meshes.size(); j++)
            {
                // m.vertexCount, m.lodCount and m.streamCount do not change
                // m.vertexOffset also does not change, because vertex offsets are local (i.e., baked into the indices)
                meshes[offs + j].IndexOffset += totalIndexDataSize;
            }

            // shift individual indices
            for (size_t j = 0; j < scene.Indices.size(); j++)
            {
                indices[totalIndexDataSize + j] += vtxOffset;
            }

            offs += (uint32_t) scene.Meshes.size();

            totalIndexDataSize += (uint32_t) scene.Indices.size();
            totalVertexDataSize += (uint32_t) scene.Vertices.size();
        }
    }

    void GraphicScene::MergeMaterials(std::span<SceneRawData> scenes)
    {
        auto& materials = s_raw_data->Materials;
        auto& files     = s_raw_data->Files;
        for (auto& scene : scenes)
        {
            for (auto& m : scene.Materials)
            {
                if (m.AlbedoTextureMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.AlbedoTextureMap];
                    files.push_back(f);
                    m.AlbedoTextureMap = (files.size() - 1);
                }
                else if (m.EmissiveTextureMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.EmissiveTextureMap];
                    files.push_back(f);
                    m.EmissiveTextureMap = (files.size() - 1);
                }
                else if (m.NormalTextureMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.NormalTextureMap];
                    files.push_back(f);
                    m.NormalTextureMap = (files.size() - 1);
                }
                else if (m.OpacityTextureMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.OpacityTextureMap];
                    files.push_back(f);
                    m.OpacityTextureMap = (files.size() - 1);
                }
            }
            MergeVector(std::span{scene.Materials}, materials);
        }
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

    std::future<int> GraphicScene::AddNodeAsync(int parent_node_id, int depth_level)
    {
        std::lock_guard lock(s_scene_node_mutex);

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
        // auto entity_wrapper = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[scene_node_identifier]);
        // entity_wrapper.AddComponent<TransformComponent>(s_raw_data->LocalTransformCollection[scene_node_identifier]);
        // entity_wrapper.AddComponent<UUIComponent>();
        // entity_wrapper.AddComponent<NameComponent>();

        co_return scene_node_identifier;
    }

    std::future<bool> GraphicScene::RemoveNodeAsync(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);

        return std::future<bool>();
    }

    int GraphicScene::GetSceneNodeParent(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].Parent;
    }

    int GraphicScene::GetSceneNodeFirstChild(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].FirstChild;
    }

    std::vector<int> GraphicScene::GetSceneNodeSiblingCollection(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);

        std::vector<int> sibling_scene_nodes = {};
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

    std::string_view GraphicScene::GetSceneNodeName(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return s_raw_data->NodeNames.contains(node_identifier) ? s_raw_data->Names[s_raw_data->NodeNames[node_identifier]] : std::string_view();
    }

    glm::mat4& GraphicScene::GetSceneNodeLocalTransform(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT((node_identifier > INVALID_SCENE_NODE_ID) && (node_identifier < s_raw_data->LocalTransformCollection.size()), "node identifier is invalid")
        return s_raw_data->LocalTransformCollection[node_identifier];
    }

    glm::mat4& GraphicScene::GetSceneNodeGlobalTransform(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < s_raw_data->GlobalTransformCollection.size(), "node identifier is invalid")
        return s_raw_data->GlobalTransformCollection[node_identifier];
    }

    const SceneNodeHierarchy& GraphicScene::GetSceneNodeHierarchy(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < s_raw_data->NodeHierarchyCollection.size(), "node identifier is invalid")
        return s_raw_data->NodeHierarchyCollection[node_identifier];
    }

    GraphicSceneEntity GraphicScene::GetSceneNodeEntityWrapper(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(s_raw_data->SceneNodeEntityMap.contains(node_identifier), "node identifier is invalid")
        return GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, s_raw_data->SceneNodeEntityMap[node_identifier]);
    }

    std::future<void> GraphicScene::SetSceneNodeNameAsync(int node_identifier, std::string_view node_name)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID, "node identifier is invalid")
        s_raw_data->Names[s_raw_data->NodeNames[node_identifier]] = node_name;
        co_return;
    }

    std::future<Meshes::MeshVNext> GraphicScene::GetSceneNodeMeshAsync(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(s_raw_data->NodeMeshes.contains(node_identifier), "node identifier is invalid")
        co_return s_raw_data->Meshes.at(s_raw_data->NodeMeshes[node_identifier]);
    }

    Ref<SceneRawData> GraphicScene::GetRawData()
    {
        std::lock_guard lock(s_scene_node_mutex);
        return s_raw_data;
    }

    void GraphicScene::SetRawData(Ref<SceneRawData>&& data)
    {
        {
            std::lock_guard lock(s_scene_node_mutex);
            s_raw_data = data;
            for (std::string_view file : data->Files)
            {
                auto index = Renderers::GraphicRenderer::GlobalTextures->Add(Textures::Texture2D::Read(file));
                s_raw_data->TextureCollection.emplace(index);
            }
        }
    }

    bool GraphicScene::HasSceneNodes()
    {
        std::lock_guard l(s_scene_node_mutex);
        return !s_raw_data->NodeHierarchyCollection.empty();
    }

    std::vector<int> GraphicScene::GetRootSceneNodes()
    {
        {
            std::lock_guard  l(s_scene_node_mutex);
            std::vector<int> root_scene_nodes;

            const auto& hierarchy = s_raw_data->NodeHierarchyCollection;
            for (uint32_t i = 0; i < hierarchy.size(); ++i)
            {
                if (hierarchy[i].Parent == SCENE_ROOT_PARENT_ID)
                {
                    root_scene_nodes.push_back(i);
                }
            }
            return root_scene_nodes;
        }
    }

    void GraphicScene::ComputeAllTransforms()
    {
        {
            std::lock_guard l(s_scene_node_mutex);

            const auto& hierarchy         = s_raw_data->NodeHierarchyCollection;
            auto&       global_transforms = s_raw_data->GlobalTransformCollection;
            auto&       local_transforms  = s_raw_data->LocalTransformCollection;
            auto&       node_changed_map  = s_raw_data->LevelSceneNodeChangedMap;
            for (auto& [level, nodes] : node_changed_map)
            {
                for (auto node : nodes)
                {
                    if (node != -1)
                    {
                        int parent = hierarchy[node].Parent;
                        if (parent != -1)
                        {
                            global_transforms[node] = global_transforms[parent] * local_transforms[node];
                        }
                    }
                }
            }
            node_changed_map.clear();
        }
    }

    void GraphicScene::MarkSceneNodeAsChanged(int node_identifier)
    {
        {
            std::lock_guard l(s_scene_node_mutex);

            auto&            hierarchy = s_raw_data->NodeHierarchyCollection;
            if (hierarchy.empty())
            {
                return;
            }

            std::queue<int>  q;
            std::vector<int> n;
            auto&            node_changed_map = s_raw_data->LevelSceneNodeChangedMap;
            q.push(node_identifier);
            while (!q.empty())
            {
                int front = q.front();
                n.push_back(front);
                q.pop();
                int first = hierarchy[front].FirstChild;
                if (first > -1)
                {
                    q.push(first);
                    for (int sibling = hierarchy[first].RightSibling; sibling != -1; sibling = hierarchy[sibling].RightSibling)
                    {
                        q.push(sibling);
                    }
                }
            }

            for (int i = 0; i < n.size(); ++i)
            {
                int node  = n[i];
                int level = hierarchy[node].DepthLevel;
                node_changed_map[level].emplace(node);
            }
        }
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

#include <pch.h>
#include <Core/Coroutine.h>
#include <Rendering/Components/CameraComponent.h>
#include <Rendering/Components/LightComponent.h>
#include <Rendering/Components/UUIComponent.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Textures/Texture2D.h>

#define NODE_PARENT_ID  -1
#define INVALID_NODE_ID -1

using namespace ZEngine::Rendering::Components;

namespace ZEngine::Rendering::Scenes
{
    std::recursive_mutex GraphicScene::s_scene_node_mutex;
    Ref<SceneRawData>    GraphicScene::s_raw_data = CreateRef<SceneRawData>();

    static entt::registry g_sceneEntityRegistry;

    entt::registry& GetEntityRegistry()
    {
        return g_sceneEntityRegistry;
    }

    /*
     * SceneRawData Implementation
     */
    int SceneRawData::AddNode(ZEngine::Rendering::Scenes::SceneRawData* scene, int parent, int depth)
    {
        if ((!scene) || (depth < 0))
        {
            return -1;
        }

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

    bool SceneRawData::SetNodeName(ZEngine::Rendering::Scenes::SceneRawData* scene, int node_id, std::string_view name)
    {
        if ((!scene) || node_id < 0)
        {
            return false;
        }
        scene->NodeNames[node_id] = scene->Names.size();
        scene->Names.push_back(name.empty() ? std::string("") : name.data());
        return true;
    }

    /*
     * SceneEntity Implementation
     */
    void SceneEntity::SetName(std::string_view name)
    {
        if (auto scene = m_weak_scene.lock())
        {
            if (m_node > 0)
            {
                auto n          = scene->NodeNames[m_node];
                scene->Names[n] = name;
            }
        }
    }

    std::string_view SceneEntity::GetName() const
    {
        std::string_view name = "";
        if (auto scene = m_weak_scene.lock())
        {
            if (m_node > 0)
            {
                auto n = scene->NodeNames[m_node];
                name   = scene->Names[n];
            }
        }
        return name;
    }

    void SceneEntity::SetTransform(glm::mat4 transform)
    {
        if (auto scene = m_weak_scene.lock())
        {
            if (m_node < 0)
            {
                return;
            }
            scene->GlobalTransformCollection[m_node] = transform;
        }
    }

    glm::mat4 SceneEntity::GetTransform() const
    {
        glm::mat4 transform = {};
        if (auto scene = m_weak_scene.lock())
        {
            if (m_node > 0)
            {
                transform = scene->GlobalTransformCollection[m_node];
            }
        }
        return transform;
    }

    int SceneEntity::GetNode() const
    {
        return m_node;
    }

    void GraphicScene::Initialize()
    {
        std::string_view root_node = "Root";
        s_raw_data->NodeNames[0]   = 0;
        s_raw_data->Names.emplace_back(root_node);
        s_raw_data->GlobalTransformCollection.emplace_back(1.0f);
        s_raw_data->LocalTransformCollection.emplace_back(1.0f);
        s_raw_data->NodeHierarchyCollection.push_back({.Parent = -1, .FirstChild = -1, .DepthLevel = 0});
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

            auto& hierarchy = s_raw_data->NodeHierarchyCollection[0];
            if (!scenes.empty() && hierarchy.FirstChild == -1)
            {
                hierarchy.FirstChild = 1;
            }

            MergeScenes(scenes);
            MergeMeshData(scenes);
            MergeMaterials(scenes);

            if (!s_raw_data->Files.empty())
            {
                for (std::string_view file : s_raw_data->Files)
                {
                    auto index = Renderers::GraphicRenderer::GlobalTextures->Add(Textures::Texture2D::Read(file));
                    s_raw_data->TextureCollection.emplace(index);
                }
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
        auto& vertices = s_raw_data->Vertices;
        auto& indices  = s_raw_data->Indices;
        auto& meshes   = s_raw_data->Meshes;

        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.Vertices}, vertices);
            MergeVector(std::span{scene.Indices}, indices);
            MergeVector(std::span{scene.Meshes}, meshes);

            uint32_t vtxOffset = s_raw_data->SVertexDataSize / 8; /* 8 is the number of per-vertex attributes: position, normal + UV */

            for (size_t j = 0; j < (uint32_t) scene.Meshes.size(); j++)
            {
                // m.vertexCount, m.lodCount and m.streamCount do not change
                // m.vertexOffset also does not change, because vertex offsets are local (i.e., baked into the indices)
                meshes[s_raw_data->SMeshCountOffset + j].IndexOffset += s_raw_data->SIndexDataSize;
            }

            // shift individual indices
            for (size_t j = 0; j < scene.Indices.size(); j++)
            {
                indices[s_raw_data->SIndexDataSize + j] += vtxOffset;
            }

            s_raw_data->SMeshCountOffset += (uint32_t) scene.Meshes.size();

            s_raw_data->SIndexDataSize += (uint32_t) scene.Indices.size();
            s_raw_data->SVertexDataSize += (uint32_t) scene.Vertices.size();
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
                if (m.AlbedoMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.AlbedoMap];
                    files.push_back(f);
                    m.AlbedoMap = (files.size() - 1);
                }

                if (m.EmissiveMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.EmissiveMap];
                    files.push_back(f);
                    m.EmissiveMap = (files.size() - 1);
                }

                if (m.SpecularMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.SpecularMap];
                    files.push_back(f);
                    m.SpecularMap = (files.size() - 1);
                }

                if (m.NormalMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.NormalMap];
                    files.push_back(f);
                    m.NormalMap = (files.size() - 1);
                }

                if (m.OpacityMap != 0xFFFFFFFF)
                {
                    auto& f = scene.Files[m.OpacityMap];
                    files.push_back(f);
                    m.OpacityMap = (files.size() - 1);
                }
            }
            MergeVector(std::span{scene.Materials}, materials);
        }
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(std::string_view entity_name, int parent_id, int depth_level)
    {
        std::unique_lock lock(s_scene_node_mutex);

        SceneEntity entity  = {};
        int         node_id = SceneRawData::AddNode(s_raw_data.get(), parent_id, depth_level);
        if (SceneRawData::SetNodeName(s_raw_data.get(), node_id, entity_name))
        {
            auto  vertices         = std::vector<float>{0, 0, 0, 0, 1, 0, 0, 0};
            auto  indices          = std::vector<uint32_t>{0};
            auto& m                = s_raw_data->Meshes.emplace_back();
            m.VertexCount          = 1;
            m.IndexCount           = 1;
            m.VertexOffset         = 0;
            m.IndexOffset          = 0;
            m.VertexUnitStreamSize = sizeof(float) * (3 + 3 + 2);
            m.IndexUnitStreamSize  = sizeof(uint32_t);
            m.StreamOffset         = (m.VertexUnitStreamSize * m.VertexOffset);
            m.IndexStreamOffset    = (m.IndexUnitStreamSize * m.IndexOffset);
            m.TotalByteSize        = (m.VertexCount * m.VertexUnitStreamSize) + (m.IndexCount * m.IndexUnitStreamSize);

            MergeVector(std::span{vertices}, s_raw_data->Vertices);
            MergeVector(std::span{indices}, s_raw_data->Indices);

            uint32_t vtx_off = s_raw_data->SVertexDataSize / 8;
            s_raw_data->Meshes[s_raw_data->SMeshCountOffset].IndexOffset += s_raw_data->SIndexDataSize;
            s_raw_data->Indices[s_raw_data->SIndexDataSize] += vtx_off;

            s_raw_data->SMeshCountOffset++;
            s_raw_data->SIndexDataSize += indices.size();
            s_raw_data->SVertexDataSize += (uint32_t) vertices.size();

            s_raw_data->NodeMeshes[node_id] = s_raw_data->Meshes.size() - 1;

            s_raw_data->MaterialNames.push_back("<unamed material>");
            s_raw_data->Materials.emplace_back();
            s_raw_data->NodeMaterials[node_id] = s_raw_data->Materials.size() - 1;

            entity = {node_id, s_raw_data.Weak()};
        }
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);
        auto             entity = co_await CreateEntityAsync(entity_name);
        entity.AddComponent<UUIComponent>(uuid);
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);
        auto             entity = co_await CreateEntityAsync(entity_name);
        entity.AddComponent<UUIComponent>(uuid_string);
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::GetEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(s_scene_node_mutex);

        int   node       = -1;
        auto& node_names = s_raw_data->NodeNames;
        auto& names      = s_raw_data->Names;

        for (auto& [id, name] : node_names)
        {
            if (names[name] == entity_name)
            {
                node = id;
            }
        }

        if (node == -1)
        {
            ZENGINE_CORE_ERROR("An entity with name {0} deosn't exist", entity_name.data())
        }
        co_return SceneEntity{node, s_raw_data.Weak()};
    }

    std::future<bool> GraphicScene::RemoveEntityAsync(const SceneEntity& entity)
    {
        co_return false;
        // std::unique_lock lock(s_scene_node_mutex);
        // if (!s_raw_data->EntityRegistry->valid(entity))
        //{
        //     ZENGINE_CORE_ERROR("This entity is no longer valid")
        //     co_return false;
        // }
        // s_raw_data->EntityRegistry->destroy(entity);
        // co_return true;
    }

    std::future<bool> GraphicScene::RemoveNodeAsync(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return std::future<bool>();
    }

    int GraphicScene::GetSceneNodeParent(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].Parent;
    }

    int GraphicScene::GetSceneNodeFirstChild(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return (node_identifier < 0) ? INVALID_NODE_ID : s_raw_data->NodeHierarchyCollection[node_identifier].FirstChild;
    }

    std::vector<int> GraphicScene::GetSceneNodeSiblingCollection(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);

        std::vector<int> sibling_scene_nodes = {};
        if (node_identifier < 0)
        {
            return sibling_scene_nodes;
        }

        for (auto sibling = s_raw_data->NodeHierarchyCollection[node_identifier].RightSibling; sibling != INVALID_NODE_ID;
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
        ZENGINE_VALIDATE_ASSERT((node_identifier > INVALID_NODE_ID) && (node_identifier < s_raw_data->LocalTransformCollection.size()), "node identifier is invalid")
        return s_raw_data->LocalTransformCollection[node_identifier];
    }

    glm::mat4& GraphicScene::GetSceneNodeGlobalTransform(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID && node_identifier < s_raw_data->GlobalTransformCollection.size(), "node identifier is invalid")
        return s_raw_data->GlobalTransformCollection[node_identifier];
    }

    const SceneNodeHierarchy& GraphicScene::GetSceneNodeHierarchy(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID && node_identifier < s_raw_data->NodeHierarchyCollection.size(), "node identifier is invalid")
        return s_raw_data->NodeHierarchyCollection[node_identifier];
    }

    SceneEntity GraphicScene::GetSceneNodeEntityWrapper(int node_identifier)
    {
        std::lock_guard lock(s_scene_node_mutex);
        return SceneEntity{node_identifier, s_raw_data.Weak()};
    }

    std::future<void> GraphicScene::SetSceneNodeNameAsync(int node_identifier, std::string_view node_name)
    {
        std::lock_guard lock(s_scene_node_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID, "node identifier is invalid")
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
        {
            std::lock_guard lock(s_scene_node_mutex);
            s_raw_data->DirectionalLights.clear();
            s_raw_data->PointLights.clear();
            s_raw_data->SpotLights.clear();
            s_raw_data->DirectionalLights.shrink_to_fit();
            s_raw_data->PointLights.shrink_to_fit();
            s_raw_data->SpotLights.shrink_to_fit();
            auto light_cmp = g_sceneEntityRegistry.view<LightComponent>();
            for (auto handle : light_cmp)
            {
                auto light     = light_cmp.get<LightComponent>(handle).GetLight();
                auto ligh_type = light->GetLightType();
                switch (ligh_type)
                {
                    case Lights::LightType::DIRECTIONAL:
                    {
                        auto directional = reinterpret_cast<Lights::DirectionalLight*>(light.get());
                        s_raw_data->DirectionalLights.push_back(directional->GPUPackedData());
                    }
                    break;
                    case Lights::LightType::POINT:
                    {
                        auto point = reinterpret_cast<Lights::PointLight*>(light.get());
                        s_raw_data->PointLights.push_back(point->GPUPackedData());
                    }
                    break;
                    case Lights::LightType::SPOT:
                    {
                        auto spot = reinterpret_cast<Lights::Spotlight*>(light.get());
                        s_raw_data->SpotLights.push_back(spot->GPUPackedData());
                    }
                    break;
                }
            }
        }
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
                if (hierarchy[i].Parent == NODE_PARENT_ID)
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

            auto& hierarchy = s_raw_data->NodeHierarchyCollection;
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

    SceneEntity GraphicScene::GetPrimariyCameraEntity()
    {
        SceneEntity camera_entity;

        // auto view_cameras = s_raw_data->EntityRegistry->view<CameraComponent>();
        // for (auto entity : view_cameras)
        //{
        //     auto& component = view_cameras.get<CameraComponent>(entity);
        //     if (component.IsPrimaryCamera)
        //     {
        //         camera_entity = GraphicSceneEntity::CreateWrapper(s_raw_data->EntityRegistry, entity);
        //         break;
        //     }
        // }
        return camera_entity;
    }
} // namespace ZEngine::Rendering::Scenes

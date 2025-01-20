#include <pch.h>
#include <Core/Coroutine.h>
#include <Renderers/GraphicRenderer.h>
#include <Rendering/Components/CameraComponent.h>
#include <Rendering/Components/LightComponent.h>
#include <Rendering/Components/UUIComponent.h>
#include <Rendering/Scenes/GraphicScene.h>

#define NODE_PARENT_ID  -1
#define INVALID_NODE_ID -1

using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Scenes
{
    static entt::registry g_sceneEntityRegistry;

    entt::registry&       GetEntityRegistry()
    {
        return g_sceneEntityRegistry;
    }

    /*
     * SceneRawData Implementation
     */
    int SceneRawData::AddNode(int parent, int depth)
    {
        if (depth < 0)
        {
            return INVALID_NODE_ID;
        }

        int node_id = (int) NodeHierarchies.size();

        NodeHierarchies.push_back({.Parent = parent});
        LocalTransforms.emplace_back(1.0f);
        GlobalTransforms.emplace_back(1.0f);

        if (parent > -1)
        {
            int first_child = NodeHierarchies[parent].FirstChild;

            if (first_child == -1)
            {
                NodeHierarchies[parent].FirstChild = node_id;
            }
            else
            {
                int right_sibling = NodeHierarchies[first_child].RightSibling;
                if (right_sibling > -1)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; NodeHierarchies[right_sibling].RightSibling != -1; right_sibling = NodeHierarchies[right_sibling].RightSibling)
                    {
                    }
                    NodeHierarchies[right_sibling].RightSibling = node_id;
                }
                else
                {
                    NodeHierarchies[first_child].RightSibling = node_id;
                }
            }
        }
        NodeHierarchies[node_id].DepthLevel   = depth;
        NodeHierarchies[node_id].RightSibling = -1;
        NodeHierarchies[node_id].FirstChild   = -1;

        return node_id;
    }

    bool SceneRawData::SetNodeName(int node_id, std::string_view name)
    {
        if (node_id < 0)
        {
            return false;
        }
        NodeNames[node_id] = Names.size();
        Names.emplace_back(name.empty() ? std::string("") : name.data());
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
            scene->GlobalTransforms[m_node] = transform;
        }
    }

    glm::mat4 SceneEntity::GetTransform() const
    {
        glm::mat4 transform = {};
        if (auto scene = m_weak_scene.lock())
        {
            if (m_node > 0)
            {
                transform = scene->GlobalTransforms[m_node];
            }
        }
        return transform;
    }

    int SceneEntity::GetNode() const
    {
        return m_node;
    }

    /*
     * Graphic Scene implementation
     */
    GraphicScene::GraphicScene()
    {
        std::string_view root_node = "Root";
        SceneData                  = CreateRef<SceneRawData>();
        SceneData->NodeNames[0]    = 0;

        SceneData->Names.emplace_back(root_node);
        SceneData->GlobalTransforms.emplace_back(1.0f);
        SceneData->LocalTransforms.emplace_back(1.0f);
        SceneData->NodeHierarchies.push_back({.Parent = -1, .FirstChild = -1, .DepthLevel = 0});

        SceneData->Vertices      = {0, 0, 0, 0, 0, 0, 0, 0};
        SceneData->Indices       = {0};
        SceneData->Materials     = {Meshes::MeshMaterial{}};
        SceneData->MaterialFiles = {Meshes::MaterialFile{}};
        SceneData->DrawData      = {
            DrawData{.TransformIndex = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 1, .IndexCount = 1}
        };
    }

    void GraphicScene::InitOrResetDrawBuffer(Hardwares::VulkanDevice* device, Renderers::RenderGraph* render_graph, Renderers::AsyncResourceLoader* async_loader)
    {
        auto                               draw_count         = SceneData->NodeMeshes.size();
        std::vector<VkDrawIndirectCommand> indirect_commmands = {};

        if (draw_count)
        {
            SceneData->DrawData.resize(draw_count);
            indirect_commmands.resize(draw_count);

            int i = 0;
            for (auto& [node, mesh] : SceneData->NodeMeshes)
            {
                DrawData& draw_data      = SceneData->DrawData[i];
                draw_data.TransformIndex = node;
                draw_data.MaterialIndex  = SceneData->NodeMaterials[node];
                draw_data.VertexOffset   = SceneData->Meshes[mesh].VertexOffset;
                draw_data.IndexOffset    = SceneData->Meshes[mesh].IndexOffset;
                draw_data.VertexCount    = SceneData->Meshes[mesh].VertexCount;
                draw_data.IndexCount     = SceneData->Meshes[mesh].IndexCount;

                ++i;
            }
        }
        else
        {
            // We use the default data
            indirect_commmands.resize(SceneData->DrawData.size());
        }

        for (uint32_t i = 0; i < SceneData->DrawData.size(); ++i)
        {
            indirect_commmands[i] = {
                .vertexCount   = SceneData->DrawData[i].IndexCount,
                .instanceCount = 1,
                .firstVertex   = 0,
                .firstInstance = i,
            };
        }

        for (int i = 0; i < SceneData->Materials.size(); ++i)
        {
            auto& mat       = SceneData->Materials[i];
            auto& mat_files = SceneData->MaterialFiles[i];

            if (!std::string_view(mat_files.AlbedoTexture).empty())
            {
                auto handle = async_loader->LoadTextureFile(mat_files.AlbedoTexture);
                if (handle)
                {
                    mat.AlbedoMap = handle.Index;
                }
            }

            if (!std::string_view(mat_files.EmissiveTexture).empty())
            {
                auto handle = async_loader->LoadTextureFile(mat_files.EmissiveTexture);
                if (handle)
                {
                    mat.EmissiveMap = handle.Index;
                }
            }

            if (!std::string_view(mat_files.NormalTexture).empty())
            {
                auto handle = async_loader->LoadTextureFile(mat_files.NormalTexture);
                if (handle)
                {
                    mat.NormalMap = handle.Index;
                }
            }

            if (!std::string_view(mat_files.OpacityTexture).empty())
            {
                auto handle = async_loader->LoadTextureFile(mat_files.OpacityTexture);
                if (handle)
                {
                    mat.OpacityMap = handle.Index;
                }
            }

            if (!std::string_view(mat_files.SpecularTexture).empty())
            {
                auto handle = async_loader->LoadTextureFile(mat_files.SpecularTexture);
                if (handle)
                {
                    mat.SpecularMap = handle.Index;
                }
            }
        }

        SceneData->TransformBufferHandle        = device->CreateStorageBufferSet();
        SceneData->VertexBufferHandle           = device->CreateStorageBufferSet();
        SceneData->IndexBufferHandle            = device->CreateStorageBufferSet();
        SceneData->MaterialBufferHandle         = device->CreateStorageBufferSet();
        SceneData->IndirectDataDrawBufferHandle = device->CreateStorageBufferSet();
        SceneData->IndirectBufferHandle         = device->CreateIndirectBufferSet();

        auto& transform_buf                     = device->StorageBufferSetManager.Access(SceneData->TransformBufferHandle);
        auto& vert_buf                          = device->StorageBufferSetManager.Access(SceneData->VertexBufferHandle);
        auto& ind_buf                           = device->StorageBufferSetManager.Access(SceneData->IndexBufferHandle);
        auto& material_buf                      = device->StorageBufferSetManager.Access(SceneData->MaterialBufferHandle);
        auto& indirect_datadraw_buf             = device->StorageBufferSetManager.Access(SceneData->IndirectDataDrawBufferHandle);
        auto& indirect_buf                      = device->IndirectBufferSetManager.Access(SceneData->IndirectBufferHandle);

        for (unsigned i = 0; i < device->SwapchainImageCount; ++i)
        {
            transform_buf->SetData<glm::mat4>(i, SceneData->GlobalTransforms);
            vert_buf->SetData<float>(i, SceneData->Vertices);
            ind_buf->SetData<uint32_t>(i, SceneData->Indices);
            material_buf->SetData<Meshes::MeshMaterial>(i, SceneData->Materials);
            indirect_datadraw_buf->SetData<DrawData>(i, SceneData->DrawData);
            indirect_buf->SetData<VkDrawIndirectCommand>(i, indirect_commmands);
        }

        render_graph->MarkAsDirty = true;
        IsDrawDataDirty           = false;
    }

    void GraphicScene::SetRootNodeName(std::string_view name)
    {
        {
            std::lock_guard l(m_mutex);
            if ((!SceneData) || SceneData->Names.empty())
            {
                return;
            }
            SceneData->Names[0] = name;
        }
    }

    void GraphicScene::Merge(std::span<SceneRawData> scenes)
    {
        {
            std::lock_guard l(m_mutex);

            auto&           hierarchy = SceneData->NodeHierarchies[0];
            if (!scenes.empty() && hierarchy.FirstChild == -1)
            {
                hierarchy.FirstChild = 1;
            }

            MergeScenes(scenes);
            MergeMeshData(scenes);
            MergeMaterials(scenes);
        }
    }

    void GraphicScene::MergeScenes(std::span<SceneRawData> scenes)
    {
        auto& hierarchy        = SceneData->NodeHierarchies;
        auto& global_transform = SceneData->GlobalTransforms;
        auto& local_transform  = SceneData->LocalTransforms;
        auto& names            = SceneData->Names;
        auto& materialNames    = SceneData->MaterialNames;

        int   offs             = 1;
        int   mesh_offset      = 0;
        int   material_offset  = 0;
        int   name_offset      = (int) names.size();

        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.NodeHierarchies}, hierarchy);
            MergeVector(std::span{scene.LocalTransforms}, local_transform);
            MergeVector(std::span{scene.GlobalTransforms}, global_transform);

            MergeVector(std::span{scene.Names}, names);
            MergeVector(std::span{scene.MaterialNames}, materialNames);

            int node_count = scene.NodeHierarchies.size();

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

            MergeMap(scene.NodeMeshes, SceneData->NodeMeshes, offs, mesh_offset);
            MergeMap(scene.NodeNames, SceneData->NodeNames, offs, name_offset);
            MergeMap(scene.NodeMaterials, SceneData->NodeMaterials, offs, material_offset);

            offs            += node_count;

            material_offset += scene.MaterialNames.size();
            name_offset     += scene.Names.size();
            mesh_offset     += scene.Meshes.size();
        }

        offs    = 1;
        int idx = 0;
        for (auto& scene : scenes)
        {
            int  nodeCount                = (int) scene.NodeHierarchies.size();
            bool isLast                   = (idx == scenes.size() - 1);
            // calculate new next sibling for the old scene roots
            int  next                     = isLast ? -1 : offs + nodeCount;
            hierarchy[offs].RightSibling  = next;
            // attach to new root
            hierarchy[offs].Parent        = 0;

            offs                         += nodeCount;
            idx++;
        }

        for (int i = 1; i < hierarchy.size(); ++i)
        {
            hierarchy[i].DepthLevel++;
        }
    }

    void GraphicScene::MergeMeshData(std::span<SceneRawData> scenes)
    {
        auto& vertices = SceneData->Vertices;
        auto& indices  = SceneData->Indices;
        auto& meshes   = SceneData->Meshes;

        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.Vertices}, vertices);
            MergeVector(std::span{scene.Indices}, indices);
            MergeVector(std::span{scene.Meshes}, meshes);

            uint32_t vtxOffset = SceneData->SVertexDataSize / 8; /* 8 is the number of per-vertex attributes: position, normal + UV */

            for (size_t j = 0; j < (uint32_t) scene.Meshes.size(); j++)
            {
                // m.vertexCount, m.lodCount and m.streamCount do not change
                // m.vertexOffset also does not change, because vertex offsets are local (i.e., baked into the indices)
                meshes[SceneData->SMeshCountOffset + j].IndexOffset += SceneData->SIndexDataSize;
            }

            // shift individual indices
            for (size_t j = 0; j < scene.Indices.size(); j++)
            {
                indices[SceneData->SIndexDataSize + j] += vtxOffset;
            }

            SceneData->SMeshCountOffset += (uint32_t) scene.Meshes.size();

            SceneData->SIndexDataSize   += (uint32_t) scene.Indices.size();
            SceneData->SVertexDataSize  += (uint32_t) scene.Vertices.size();
        }
    }

    void GraphicScene::MergeMaterials(std::span<SceneRawData> scenes)
    {
        auto& materials      = SceneData->Materials;
        auto& material_files = SceneData->MaterialFiles;
        for (auto& scene : scenes)
        {
            MergeVector(std::span{scene.Materials}, materials);
            MergeVector(std::span{scene.MaterialFiles}, material_files);
        }
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(std::string_view entity_name, int parent_id, int depth_level)
    {
        std::unique_lock lock(m_mutex);

        SceneEntity      entity  = {};
        int              node_id = SceneData->AddNode(parent_id, depth_level);
        if (SceneData->SetNodeName(node_id, entity_name))
        {
            auto  vertices         = std::vector<float>{0, 0, 0, 0, 1, 0, 0, 0};
            auto  indices          = std::vector<uint32_t>{0};
            auto& m                = SceneData->Meshes.emplace_back();
            m.VertexCount          = 1;
            m.IndexCount           = 1;
            m.VertexOffset         = 0;
            m.IndexOffset          = 0;
            m.VertexUnitStreamSize = sizeof(float) * (3 + 3 + 2);
            m.IndexUnitStreamSize  = sizeof(uint32_t);
            m.StreamOffset         = (m.VertexUnitStreamSize * m.VertexOffset);
            m.IndexStreamOffset    = (m.IndexUnitStreamSize * m.IndexOffset);
            m.TotalByteSize        = (m.VertexCount * m.VertexUnitStreamSize) + (m.IndexCount * m.IndexUnitStreamSize);

            MergeVector(std::span{vertices}, SceneData->Vertices);
            MergeVector(std::span{indices}, SceneData->Indices);

            uint32_t vtx_off                                            = SceneData->SVertexDataSize / 8;
            SceneData->Meshes[SceneData->SMeshCountOffset].IndexOffset += SceneData->SIndexDataSize;
            SceneData->Indices[SceneData->SIndexDataSize]              += vtx_off;

            SceneData->SMeshCountOffset++;
            SceneData->SIndexDataSize      += indices.size();
            SceneData->SVertexDataSize     += (uint32_t) vertices.size();

            SceneData->NodeMeshes[node_id]  = SceneData->Meshes.size() - 1;

            SceneData->MaterialNames.push_back("<unamed material>");
            SceneData->Materials.emplace_back();
            SceneData->NodeMaterials[node_id] = SceneData->Materials.size() - 1;

            entity                            = {node_id, SceneData.Weak()};
        }
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name)
    {
        std::unique_lock lock(m_mutex);
        auto             entity = co_await CreateEntityAsync(entity_name);
        entity.AddComponent<UUIComponent>(uuid);
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name)
    {
        std::unique_lock lock(m_mutex);
        auto             entity = co_await CreateEntityAsync(entity_name);
        entity.AddComponent<UUIComponent>(uuid_string);
        co_return entity;
    }

    std::future<SceneEntity> GraphicScene::GetEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(m_mutex);

        int              node       = -1;
        auto&            node_names = SceneData->NodeNames;
        auto&            names      = SceneData->Names;

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
        co_return SceneEntity{node, SceneData.Weak()};
    }

    std::future<bool> GraphicScene::RemoveEntityAsync(const SceneEntity& entity)
    {
        co_return false;
        // std::unique_lock lock(m_mutex);
        // if (!SceneData->EntityRegistry->valid(entity))
        //{
        //     ZENGINE_CORE_ERROR("This entity is no longer valid")
        //     co_return false;
        // }
        // SceneData->EntityRegistry->destroy(entity);
        // co_return true;
    }

    std::future<bool> GraphicScene::RemoveNodeAsync(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        return std::future<bool>();
    }

    int GraphicScene::GetSceneNodeParent(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        return (node_identifier < 0) ? INVALID_NODE_ID : SceneData->NodeHierarchies[node_identifier].Parent;
    }

    int GraphicScene::GetSceneNodeFirstChild(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        return (node_identifier < 0) ? INVALID_NODE_ID : SceneData->NodeHierarchies[node_identifier].FirstChild;
    }

    std::vector<int> GraphicScene::GetSceneNodeSiblingCollection(int node_identifier)
    {
        std::lock_guard  lock(m_mutex);

        std::vector<int> sibling_scene_nodes = {};
        if (node_identifier < 0)
        {
            return sibling_scene_nodes;
        }

        for (auto sibling = SceneData->NodeHierarchies[node_identifier].RightSibling; sibling != INVALID_NODE_ID; sibling = SceneData->NodeHierarchies[sibling].RightSibling)
        {
            sibling_scene_nodes.push_back(sibling);
        }

        return sibling_scene_nodes;
    }

    std::string_view GraphicScene::GetSceneNodeName(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        return SceneData->NodeNames.contains(node_identifier) ? SceneData->Names[SceneData->NodeNames[node_identifier]] : std::string_view();
    }

    glm::mat4& GraphicScene::GetSceneNodeLocalTransform(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT((node_identifier > INVALID_NODE_ID) && (node_identifier < SceneData->LocalTransforms.size()), "node identifier is invalid")
        return SceneData->LocalTransforms[node_identifier];
    }

    glm::mat4& GraphicScene::GetSceneNodeGlobalTransform(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID && node_identifier < SceneData->GlobalTransforms.size(), "node identifier is invalid")
        return SceneData->GlobalTransforms[node_identifier];
    }

    const SceneNodeHierarchy& GraphicScene::GetSceneNodeHierarchy(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID && node_identifier < SceneData->NodeHierarchies.size(), "node identifier is invalid")
        return SceneData->NodeHierarchies[node_identifier];
    }

    SceneEntity GraphicScene::GetSceneNodeEntityWrapper(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        return SceneEntity{node_identifier, SceneData.Weak()};
    }

    std::future<void> GraphicScene::SetSceneNodeNameAsync(int node_identifier, std::string_view node_name)
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_NODE_ID, "node identifier is invalid")
        SceneData->Names[SceneData->NodeNames[node_identifier]] = node_name;
        co_return;
    }

    std::future<Meshes::MeshVNext> GraphicScene::GetSceneNodeMeshAsync(int node_identifier)
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(SceneData->NodeMeshes.contains(node_identifier), "node identifier is invalid")
        co_return SceneData->Meshes.at(SceneData->NodeMeshes[node_identifier]);
    }

    Ref<SceneRawData> GraphicScene::GetRawData()
    {
        {
            std::lock_guard lock(m_mutex);
            SceneData->DirectionalLights.clear();
            SceneData->PointLights.clear();
            SceneData->SpotLights.clear();
            SceneData->DirectionalLights.shrink_to_fit();
            SceneData->PointLights.shrink_to_fit();
            SceneData->SpotLights.shrink_to_fit();
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
                        SceneData->DirectionalLights.push_back(directional->GPUPackedData());
                    }
                    break;
                    case Lights::LightType::POINT:
                    {
                        auto point = reinterpret_cast<Lights::PointLight*>(light.get());
                        SceneData->PointLights.push_back(point->GPUPackedData());
                    }
                    break;
                    case Lights::LightType::SPOT:
                    {
                        auto spot = reinterpret_cast<Lights::Spotlight*>(light.get());
                        SceneData->SpotLights.push_back(spot->GPUPackedData());
                    }
                    break;
                }
            }
        }
        return SceneData;
    }

    bool GraphicScene::HasSceneNodes()
    {
        std::lock_guard l(m_mutex);
        return !SceneData->NodeHierarchies.empty();
    }

    std::vector<int> GraphicScene::GetRootSceneNodes()
    {
        {
            std::lock_guard  l(m_mutex);
            std::vector<int> root_scene_nodes;

            const auto&      hierarchy = SceneData->NodeHierarchies;
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
            std::lock_guard l(m_mutex);

            const auto&     hierarchy         = SceneData->NodeHierarchies;
            auto&           global_transforms = SceneData->GlobalTransforms;
            auto&           local_transforms  = SceneData->LocalTransforms;
            auto&           node_changed_map  = SceneData->LevelSceneNodeChangedMap;
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
            std::lock_guard l(m_mutex);

            auto&           hierarchy = SceneData->NodeHierarchies;
            if (hierarchy.empty())
            {
                return;
            }

            std::queue<int>  q;
            std::vector<int> n;
            auto&            node_changed_map = SceneData->LevelSceneNodeChangedMap;
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

        // auto view_cameras = SceneData->EntityRegistry->view<CameraComponent>();
        // for (auto entity : view_cameras)
        //{
        //     auto& component = view_cameras.get<CameraComponent>(entity);
        //     if (component.IsPrimaryCamera)
        //     {
        //         camera_entity = GraphicSceneEntity::CreateWrapper(SceneData->EntityRegistry, entity);
        //         break;
        //     }
        // }
        return camera_entity;
    }
} // namespace ZEngine::Rendering::Scenes

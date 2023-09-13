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
    std::vector<glm::mat4>                          GraphicScene::m_local_transform_collection;
    std::vector<glm::mat4>                          GraphicScene::m_global_transform_collection;
    std::vector<Meshes::MeshVNext>                  GraphicScene::m_mesh_vnext_list;
    std::vector<NodeHierarchy>                      GraphicScene::m_node_hierarchy_collection;
    std::unordered_map<uint32_t, uint32_t>          GraphicScene::m_scene_node_mesh_map;
    std::unordered_map<uint32_t, uint32_t>          GraphicScene::m_scene_node_material_map;
    std::unordered_map<uint32_t, entt::entity>      GraphicScene::m_scene_node_entity_map;
    std::unordered_map<uint32_t, std::string>       GraphicScene::m_scene_node_name_map;
    std::unordered_map<uint32_t, std::string>       GraphicScene::m_scene_node_material_name_map;
    std::unordered_map<uint32_t, Meshes::MeshVNext> GraphicScene::m_mesh_identifier_data_map;
    Ref<entt::registry>                             GraphicScene::m_entity_registry;
    std::recursive_mutex                            GraphicScene::s_mutex;

    GraphicScene::~GraphicScene() {}

    void GraphicScene::Initialize()
    {
        Renderers::GraphicRenderer::Initialize();
    }

    void GraphicScene::Deinitialize()
    {
        Renderers::GraphicRenderer::Deinitialize();
        for (auto& mesh : m_mesh_vnext_list)
        {
            mesh.Flush();
        }
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(s_mutex);

        auto scene_node_identifier = co_await AddNodeAsync(SCENE_ROOT_PARENT_ID, SCENE_ROOT_DEPTH_LEVEL);
        co_await SetSceneNodeNameAsync(scene_node_identifier, entity_name);

        auto  scene_entity   = GraphicSceneEntity::CreateWrapper(m_entity_registry, m_scene_node_entity_map[scene_node_identifier]);
        auto& name_component = scene_entity.GetComponent<NameComponent>();
        name_component.Name  = entity_name;
        co_return scene_entity;
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name)
    {
        std::unique_lock lock(s_mutex);
        auto             scene_entity = co_await CreateEntityAsync(entity_name);
        scene_entity.AddComponent<UUIComponent>(uuid);
        co_return scene_entity;
    }

    std::future<GraphicSceneEntity> GraphicScene::CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name)
    {
        std::unique_lock lock(s_mutex);
        auto             scene_entity = co_await CreateEntityAsync(entity_name);
        scene_entity.AddComponent<UUIComponent>(uuid_string);
        co_return scene_entity;
    }

    std::future<Entities::GraphicSceneEntity> GraphicScene::GetEntityAsync(std::string_view entity_name)
    {
        std::unique_lock lock(s_mutex);

        entt::entity entity_handle{entt::null};
        auto         views = m_entity_registry->view<NameComponent>();
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

        co_return GraphicSceneEntity::CreateWrapper(m_entity_registry, entity_handle);
    }

    std::future<bool> GraphicScene::RemoveEntityAsync(const Entities::GraphicSceneEntity& entity)
    {
        std::unique_lock lock(s_mutex);
        if (!m_entity_registry->valid(entity))
        {
            ZENGINE_CORE_ERROR("This entity is no longer valid")
            co_return false;
        }
        m_entity_registry->destroy(entity);
        co_return true;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view entity_name)
    {
        auto graphic_entity = GraphicSceneEntity::CreateWrapper(m_entity_registry, m_entity_registry->create());
        graphic_entity.AddComponent<UUIComponent>();
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(uuids::uuid uuid, std::string_view entity_name)
    {
        auto graphic_entity = GraphicSceneEntity::CreateWrapper(m_entity_registry, m_entity_registry->create());
        graphic_entity.AddComponent<UUIComponent>(uuid);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view uuid_string, std::string_view entity_name)
    {
        auto graphic_entity = GraphicSceneEntity::CreateWrapper(m_entity_registry, m_entity_registry->create());
        graphic_entity.AddComponent<UUIComponent>(uuid_string);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::GetEntity(std::string_view entity_name)
    {
        entt::entity entity_handle{entt::null};
        auto         views = m_entity_registry->view<NameComponent>();
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

        return GraphicSceneEntity::CreateWrapper(m_entity_registry, entity_handle);
    }

    GraphicSceneEntity GraphicScene::GetEntity(int mouse_pos_x, int mouse_pos_y)
    {
        // int entity_id = m_renderer->GetFrameBuffer()->ReadPixelAt(mouse_pos_x, mouse_pos_y, 1);
        // ZENGINE_CORE_WARN("Pixel ID: {}", entity_id)
        return GraphicSceneEntity();
    }

    void GraphicScene::RemoveEntity(const GraphicSceneEntity& entity)
    {
        if (!m_entity_registry->valid(entity))
        {
            ZENGINE_CORE_ERROR("This entity is no longer valid")
            return;
        }
        m_entity_registry->destroy(entity);
    }

    void GraphicScene::RemoveAllEntities()
    {
        m_entity_registry->clear();
    }

    void GraphicScene::RemoveInvalidEntities()
    {
        m_entity_registry->view<ValidComponent>().each([&](entt::entity handle, ValidComponent& component) {
            if (!component.IsValid)
            {
                m_entity_registry->destroy(handle);
            }
        });
    }

    void GraphicScene::InvalidateAllEntities()
    {
        m_entity_registry->view<ValidComponent>().each([&](ValidComponent& component) {
            component.IsValid = false;
        });
    }

    Ref<entt::registry> GraphicScene::GetRegistry()
    {
        return m_entity_registry;
    }

    bool GraphicScene::HasEntities() const
    {
        return !m_entity_registry->empty();
    }

    bool GraphicScene::HasInvalidEntities() const
    {
        bool found = false;
        auto view  = m_entity_registry->view<ValidComponent>();
        for (auto entity : view)
        {
            auto& component = view.get<ValidComponent>(entity);
            if (!component.IsValid)
            {
                found = true;
                break;
            }
        }
        return found;
    }

    bool GraphicScene::IsValidEntity(const GraphicSceneEntity& entity) const
    {
        return m_entity_registry->valid(entity);
    }

    int32_t GraphicScene::AddMesh(Meshes::MeshVNext&& mesh)
    {
        int32_t index{-1};
        {
            std::unique_lock lock(this->m_mutex);
            m_mesh_vnext_list.push_back(std::move(mesh));

            index = (m_mesh_vnext_list.size() - 1);
        }
        return index;
    }

    const Meshes::MeshVNext& GraphicScene::GetMesh(int32_t mesh_id) const
    {
        std::unique_lock lock(this->m_mutex);

        ZENGINE_VALIDATE_ASSERT(mesh_id >= 0 && mesh_id < (m_mesh_vnext_list.size() - 1), "Mesh ID is invalid")
        return m_mesh_vnext_list.at(mesh_id);
    }

    Meshes::MeshVNext& GraphicScene::GetMesh(int32_t mesh_id)
    {
        std::unique_lock lock(this->m_mutex);

        ZENGINE_VALIDATE_ASSERT(mesh_id >= 0 && mesh_id < m_mesh_vnext_list.size(), "Mesh ID is invalid")
        return m_mesh_vnext_list[mesh_id];
    }

    std::future<int32_t> GraphicScene::AddNodeAsync(int parent_node_id, int depth_level)
    {
        std::unique_lock lock(s_mutex);

        int scene_node_identifier = (int) m_node_hierarchy_collection.size();

        m_node_hierarchy_collection.push_back({.Parent = parent_node_id});
        m_local_transform_collection.emplace_back(1.0f);
        m_global_transform_collection.emplace_back(1.0f);

        if (parent_node_id > SCENE_ROOT_PARENT_ID)
        {
            int first_child = m_node_hierarchy_collection[parent_node_id].FirstChild;

            if (first_child == INVALID_SCENE_NODE_ID)
            {
                m_node_hierarchy_collection[parent_node_id].FirstChild = scene_node_identifier;
            }
            else
            {
                int right_sibling = m_node_hierarchy_collection[first_child].RightSibling;
                if (right_sibling > INVALID_SCENE_NODE_ID)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; m_node_hierarchy_collection[right_sibling].RightSibling != INVALID_SCENE_NODE_ID;
                         right_sibling = m_node_hierarchy_collection[right_sibling].RightSibling)
                    {
                    }
                }
                m_node_hierarchy_collection[right_sibling].RightSibling = scene_node_identifier;
            }
        }
        m_node_hierarchy_collection[scene_node_identifier].DepthLevel   = depth_level;
        m_node_hierarchy_collection[scene_node_identifier].RightSibling = INVALID_SCENE_NODE_ID;
        m_node_hierarchy_collection[scene_node_identifier].FirstChild   = INVALID_SCENE_NODE_ID;
        /*
         * Extra SceneEntity information for a SceneNode
         */
        m_scene_node_entity_map[scene_node_identifier] = m_entity_registry->create();
        auto entity_wrapper                            = GraphicSceneEntity::CreateWrapper(m_entity_registry, m_scene_node_entity_map[scene_node_identifier]);
        entity_wrapper.AddComponent<UUIComponent>();
        entity_wrapper.AddComponent<TransformComponent>();
        entity_wrapper.AddComponent<NameComponent>();

        co_return scene_node_identifier;
    }

    std::future<bool> GraphicScene::RemoveNodeAsync(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);

        return std::future<bool>();
    }

    int32_t GraphicScene::GetSceneNodeParent(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : m_node_hierarchy_collection[node_identifier].Parent;
    }

    int32_t GraphicScene::GetSceneNodeFirstChild(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        return (node_identifier < 0) ? INVALID_SCENE_NODE_ID : m_node_hierarchy_collection[node_identifier].FirstChild;
    }

    std::vector<int32_t> GraphicScene::GetSceneNodeSiblingCollection(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);

        std::vector<int32_t> sibling_scene_nodes = {};
        if (node_identifier < 0)
        {
            return sibling_scene_nodes;
        }

        for (auto sibling = m_node_hierarchy_collection[node_identifier].RightSibling; sibling != INVALID_SCENE_NODE_ID;
             sibling      = m_node_hierarchy_collection[sibling].RightSibling)
        {
            sibling_scene_nodes.push_back(sibling);
        }

        return sibling_scene_nodes;
    }

    std::string_view GraphicScene::GetSceneNodeName(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        return m_scene_node_name_map.contains(node_identifier) ? m_scene_node_name_map[node_identifier] : std::string_view();
    }

    int32_t GraphicScene::GetSceneNodeMeshIdentifier(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        return m_scene_node_mesh_map.contains(node_identifier) ? m_scene_node_mesh_map[node_identifier] : INVALID_SCENE_NODE_ID;
    }

    glm::mat4& GraphicScene::GetSceneNodeLocalTransform(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT((node_identifier > INVALID_SCENE_NODE_ID) && (node_identifier < m_local_transform_collection.size()), "node identifier is invalid")
        return m_local_transform_collection[node_identifier];
    }

    glm::mat4& GraphicScene::GetSceneNodeGlobalTransform(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < m_global_transform_collection.size(), "node identifier is invalid")
        return m_global_transform_collection[node_identifier];
    }

    const NodeHierarchy& GraphicScene::GetSceneNodeHierarchy(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID && node_identifier < m_node_hierarchy_collection.size(), "node identifier is invalid")
        return m_node_hierarchy_collection[node_identifier];
    }

    GraphicSceneEntity GraphicScene::GetSceneNodeEntityWrapper(int32_t node_identifier)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT(m_scene_node_entity_map.contains(node_identifier), "node identifier is invalid")
        return GraphicSceneEntity::CreateWrapper(m_entity_registry, m_scene_node_entity_map[node_identifier]);
    }

    std::future<void> GraphicScene::SetSceneNodeNameAsync(int32_t node_identifier, std::string_view node_name)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT(node_identifier > INVALID_SCENE_NODE_ID, "node identifier is invalid")
        m_scene_node_name_map[node_identifier] = node_name;
        co_return;
    }

    std::future<Meshes::MeshVNext> GraphicScene::GetMeshDataAsync(int32_t mesh_identifier)
    {
        std::unique_lock lock(s_mutex);
        ZENGINE_VALIDATE_ASSERT(m_mesh_identifier_data_map.contains(mesh_identifier), "node identifier is invalid")
        co_return m_mesh_identifier_data_map.at(mesh_identifier);
    }

    std::future<bool> GraphicScene::ImportAssetAsync(std::string_view asset_filename)
    {
        std::unique_lock lock(s_mutex);
        bool result = true;

        Assimp::Importer importer  = {};
        const aiScene*   scene_ptr = importer.ReadFile(asset_filename.data(), aiProcess_Triangulate | aiProcess_FlipUVs);
        if ((!scene_ptr) || scene_ptr->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene_ptr->mRootNode)
        {
            result = false;
            co_return result;
        }

        aiNode* root_node = scene_ptr->mRootNode;
        result            = co_await __TraverseAssetNodeAsync(scene_ptr, root_node, SCENE_ROOT_PARENT_ID, SCENE_ROOT_DEPTH_LEVEL);
        importer.FreeScene();

        co_return result;
    }

    bool GraphicScene::HasSceneNodes()
    {
        std::unique_lock lock(s_mutex);
        return !m_node_hierarchy_collection.empty();
    }

    std::vector<int32_t> GraphicScene::GetRootSceneNodes()
    {
        std::unique_lock     lock(s_mutex);
        std::vector<int32_t> root_scene_nodes;

        if (!HasSceneNodes())
        {
            return root_scene_nodes;
        }

        for (uint32_t i = 0; i < m_node_hierarchy_collection.size(); ++i)
        {
            if (m_node_hierarchy_collection[i].Parent == SCENE_ROOT_PARENT_ID)
            {
                root_scene_nodes.push_back(i);
            }
        }
        return root_scene_nodes;
    }

    std::future<bool> GraphicScene::__TraverseAssetNodeAsync(const aiScene* assimp_scene, aiNode* node, int parent_node, int depth_level)
    {
        std::unique_lock lock(s_mutex);

        bool result = true;
        if (!assimp_scene && !node)
        {
            result = false;
            co_return result;
        }

        auto scene_node_identifier                   = co_await AddNodeAsync(parent_node, depth_level);
        m_scene_node_name_map[scene_node_identifier] = node->mName.C_Str() ? std::string(node->mName.C_Str()) : std::string{"<unamed node>"};

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const NodeHierarchy& hierarchy   = m_node_hierarchy_collection[scene_node_identifier];
            int32_t              sub_node_id = co_await AddNodeAsync(scene_node_identifier, hierarchy.DepthLevel + 1);
            /*
             * Processing Mesh data
             */
            uint32_t mesh_id                   = node->mMeshes[i];
            aiString mesh_name                 = assimp_scene->mMeshes[mesh_id]->mName;
            m_scene_node_mesh_map[sub_node_id] = mesh_id;
            m_scene_node_name_map[sub_node_id] =
                mesh_name.C_Str() ? std::string(mesh_name.C_Str()) : fmt::format("{0}_Mesh_{1}", m_scene_node_name_map[scene_node_identifier].c_str(), i);
            co_await __ReadSceneNodeMeshDataAsync(assimp_scene, mesh_id);
            /*
             * Processing Material data
             */
            uint32_t material_id                        = assimp_scene->mMeshes[mesh_id]->mMaterialIndex;
            aiString material_name                      = assimp_scene->mMaterials[material_id]->GetName();
            m_scene_node_material_map[sub_node_id]      = material_id;
            m_scene_node_material_name_map[sub_node_id] = material_name.C_Str() ? std::string(material_name.C_Str()) : std::string{};
        }
        m_local_transform_collection[scene_node_identifier] = Helpers::ConvertToMat4(node->mTransformation);

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

    std::future<void> GraphicScene::__ReadSceneNodeMeshDataAsync(const aiScene* assimp_scene, uint32_t mesh_identifier)
    {
        std::unique_lock lock(s_mutex);

        aiMesh* assimp_mesh = assimp_scene->mMeshes[mesh_identifier];

        uint32_t              vertex_count{0};
        std::vector<float>    vertices = {};
        std::vector<uint32_t> indices  = {};

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
        for (int ix = 0; ix < assimp_mesh->mNumFaces; ++ix)
        {
            aiFace assimp_mesh_face = assimp_mesh->mFaces[ix];

            for (int j = 0; j < assimp_mesh_face.mNumIndices; ++j)
            {
                indices.push_back(assimp_mesh_face.mIndices[j]);
            }
        }

        m_mesh_identifier_data_map[mesh_identifier] = Rendering::Meshes::MeshVNext{std::move(vertices), std::move(indices), vertex_count};
        co_return;
    }

    bool GraphicScene::OnEvent(Event::CoreEvent& e)
    {
        m_entity_registry->view<CameraComponent>().each([&](CameraComponent& component) {
            if (component.IsPrimaryCamera)
            {
                component.GetCameraController()->OnEvent(e);
            }
        });
        return false;
    }

    std::future<void> GraphicScene::RequestNewSizeAsync(float width, float height)
    {
        std::unique_lock lock(m_mutex);

        if ((width > 0.0f) && (height > 0.0f))
        {
            m_scene_requested_size = {width, height};
        }
        co_return;
    }

    void GraphicScene::SetShouldReactToEvent(bool value)
    {
        m_should_react_to_event = value;
    }

    bool GraphicScene::ShouldReactToEvent() const
    {
        return m_should_react_to_event;
    }

    void GraphicScene::SetCameraController(const Ref<Controllers::ICameraController>& camera_controller)
    {
        m_camera_controller = camera_controller;
    }

    void GraphicScene::SetWindowParent(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window)
    {
        m_parent_window = window;
    }

    Ref<ZEngine::Window::CoreWindow> GraphicScene::GetWindowParent() const
    {
        return m_parent_window.lock();
    }

    GraphicSceneEntity GraphicScene::GetPrimariyCameraEntity() const
    {
        GraphicSceneEntity camera_entity;

        auto view_cameras = m_entity_registry->view<CameraComponent>();
        for (auto entity : view_cameras)
        {
            auto& component = view_cameras.get<CameraComponent>(entity);
            if (component.IsPrimaryCamera)
            {
                camera_entity = GraphicSceneEntity::CreateWrapper(m_entity_registry, entity);
                break;
            }
        }
        return camera_entity;
    }

    void GraphicScene::Update(Core::TimeStep dt)
    {
        //  Todo : Should be refactored
        m_entity_registry->view<TransformComponent, MeshComponent>().each([this](entt::entity handle, TransformComponent& transform_component, MeshComponent& mesh_component) {
            auto& mesh          = m_mesh_vnext_list[mesh_component.GetMeshID()];
            mesh.LocalTransform = transform_component.GetTransform();
        });
    }

    void GraphicScene::Render()
    {
        {
            std::unique_lock lock(m_mutex);
            if (auto controller = m_camera_controller.lock())
            {
                {
                    if ((m_last_scene_requested_size.first != m_scene_requested_size.first) || (m_last_scene_requested_size.second != m_scene_requested_size.second))
                    {
                        controller->SetAspectRatio(m_scene_requested_size.first / m_scene_requested_size.second);
                        m_last_scene_requested_size = m_scene_requested_size;
                    }
                }

                m_scene_camera = controller->GetCamera();
                if (auto camera = m_scene_camera.lock())
                {
                    /*ToDo: you revisit this ID system*/
                    auto scene_information = Renderers::Contracts::GraphicSceneLayout{
                        .ViewportWidth = (uint32_t) m_scene_requested_size.first, .ViewportHeight = (uint32_t) m_scene_requested_size.second, .GraphicScenePtr = this};

                    Renderers::GraphicRenderer::StartScene(camera);
                    for (uint32_t i = 0; i < m_mesh_vnext_list.size(); ++i)
                    {

                        Renderers::GraphicRenderer::Submit(i);
                    }
                    Renderers::GraphicRenderer::EndScene();
                    // m_renderer->EndScene();

                    if (OnSceneRenderCompleted)
                    {
                        // OnSceneRenderCompleted(m_renderer->GetOutputImage(0));
                    }
                }
            }
        }
    }

    unsigned int GraphicScene::ToTextureRepresentation() const
    {
        return 0;
        // return m_renderer->GetFrameBuffer()->GetTexture();
    }

} // namespace ZEngine::Rendering::Scenes

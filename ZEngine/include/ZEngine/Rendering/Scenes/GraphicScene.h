#pragma once
#include <vector>
#include <functional>
#include <future>
#include <Rendering/Cameras/Camera.h>
#include <ZEngineDef.h>
#include <Core/IRenderable.h>
#include <Core/IInitializable.h>
#include <Core/IUpdatable.h>
#include <Controllers/ICameraController.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <Window/CoreWindow.h>
#include <entt/entt.hpp>
#include <uuid.h>
#include <mutex>

#include <Rendering/Scenes/SceneNode.h>

#include <assimp/scene.h>


namespace ZEngine::Serializers
{
    class GraphicScene3DSerializer;
}

namespace ZEngine::Rendering::Scenes
{
    class GraphicScene : public Core::IInitializable, public Core::IUpdatable, public Core::IRenderable, public Core::IEventable
    {

    public:
        GraphicScene()
        {
            m_entity_registry = CreateRef<entt::registry>();
        }

        virtual ~GraphicScene();

        void         Initialize() override;
        virtual void Deinitialize() override;
        void         Update(Core::TimeStep dt) override;
        virtual bool OnEvent(Event::CoreEvent&) override;
        virtual void Render() override;

        virtual std::future<void> RequestNewSizeAsync(float, float);
        uint32_t                  ToTextureRepresentation() const;

        void SetShouldReactToEvent(bool value);
        bool ShouldReactToEvent() const;

        void                             SetCameraController(const Ref<Controllers::ICameraController>& camera_controller);
        void                             SetWindowParent(const Ref<ZEngine::Window::CoreWindow>& window);
        Ref<ZEngine::Window::CoreWindow> GetWindowParent() const;

        Entities::GraphicSceneEntity GetPrimariyCameraEntity() const;

    public:
        /*
         * SceneEntity operations
         */
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(std::string_view entity_name = "Empty Entity");
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name);
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name);
        static std::future<Entities::GraphicSceneEntity> GetEntityAsync(std::string_view entity_name);
        static std::future<bool>                         RemoveEntityAsync(const Entities::GraphicSceneEntity& entity);
        static Ref<entt::registry>                       GetRegistry();
        /*
         * ToDo : Should be deprecated
         */
        Entities::GraphicSceneEntity CreateEntity(std::string_view entity_name = "empty entity");
        Entities::GraphicSceneEntity CreateEntity(uuids::uuid uuid, std::string_view entity_name = "empty entity");
        Entities::GraphicSceneEntity CreateEntity(std::string_view uuid_string, std::string_view entity_name = "empty entity");
        Entities::GraphicSceneEntity GetEntity(std::string_view entity_name);
        Entities::GraphicSceneEntity GetEntity(int mouse_pos_x, int mouse_pos_y);
        void                         RemoveEntity(const Entities::GraphicSceneEntity& entity);
        void                         RemoveAllEntities();
        void                         RemoveInvalidEntities();
        void                         InvalidateAllEntities();

        bool HasEntities() const;
        bool HasInvalidEntities() const;
        bool IsValidEntity(const Entities::GraphicSceneEntity&) const;

        int32_t                  AddMesh(Meshes::MeshVNext&& mesh);
        const Meshes::MeshVNext& GetMesh(int32_t mesh_id) const;
        Meshes::MeshVNext&       GetMesh(int32_t mesh_id);

        /*
         * SceneNode operations
         */
        static std::future<int32_t>           AddNodeAsync(int parent_node, int depth_level);
        static std::future<bool>              RemoveNodeAsync(int32_t node_identifier);
        static int32_t                        GetSceneNodeParent(int32_t node_identifier);
        static int32_t                        GetSceneNodeFirstChild(int32_t node_identifier);
        static std::vector<int32_t>           GetSceneNodeSiblingCollection(int32_t node_identifier);
        static std::string_view               GetSceneNodeName(int32_t node_identifier);
        static int32_t                        GetSceneNodeMeshIdentifier(int32_t node_identifier);
        static glm::mat4&                     GetSceneNodeLocalTransform(int32_t node_identifier);
        static glm::mat4&                     GetSceneNodeGlobalTransform(int32_t node_identifier);
        static const NodeHierarchy&           GetSceneNodeHierarchy(int32_t node_identifier);
        static Entities::GraphicSceneEntity   GetSceneNodeEntityWrapper(int32_t node_identifier);
        static std::future<void>              SetSceneNodeNameAsync(int32_t node_identifier, std::string_view node_name);
        static std::future<Meshes::MeshVNext> GetMeshDataAsync(int32_t mesh_identifier);
        /*
         * Scene Graph operations
         */
        static bool                 HasSceneNodes();
        static uint32_t             GetSceneNodeCount() = delete;
        static std::vector<int32_t> GetRootSceneNodes();
        static std::future<bool>    ImportAssetAsync(std::string_view asset_filename);
        static std::future<void>    LoadSceneAsync(std::string_view scene_file) = delete;

        std::function<void(Renderers::Contracts::FramebufferViewLayout)> OnSceneRenderCompleted;

    protected:
        WeakRef<Controllers::ICameraController> m_camera_controller;
        WeakRef<Cameras::Camera>                m_scene_camera;

        /*
         * This internal defragmented storage represents SceneNode struct.
         * The access is index based.
         */
        static std::vector<glm::mat4>                          m_local_transform_collection;
        static std::vector<glm::mat4>                          m_global_transform_collection;
        static std::vector<Meshes::MeshVNext>                  m_mesh_vnext_list; // todo deprecated..
        static std::vector<NodeHierarchy>                      m_node_hierarchy_collection;
        static std::unordered_map<uint32_t, uint32_t>          m_scene_node_mesh_map;
        static std::unordered_map<uint32_t, uint32_t>          m_scene_node_material_map;
        static std::unordered_map<uint32_t, entt::entity>      m_scene_node_entity_map;
        static std::unordered_map<uint32_t, std::string>       m_scene_node_name_map;
        static std::unordered_map<uint32_t, std::string>       m_scene_node_material_name_map;
        static std::unordered_map<uint32_t, Meshes::MeshVNext> m_mesh_identifier_data_map;
        static Ref<entt::registry>                             m_entity_registry;

    private:
        static std::future<bool> __TraverseAssetNodeAsync(const aiScene* assimp_scene, aiNode* node, int parent_node, int depth_level);
        static std::future<void> __ReadSceneNodeMeshDataAsync(const aiScene* assimp_scene, uint32_t mesh_identifier);

    private:
        bool                                          m_should_react_to_event{true};
        std::pair<float, float>                       m_scene_requested_size{0.0f, 0.0f};
        std::pair<float, float>                       m_last_scene_requested_size{0.0f, 0.0f};
        ZEngine::WeakRef<ZEngine::Window::CoreWindow> m_parent_window;
        mutable std::mutex                            m_mutex;
        static std::recursive_mutex                   s_mutex;

        friend class ZEngine::Serializers::GraphicScene3DSerializer;
    };
} // namespace ZEngine::Rendering::Scenes

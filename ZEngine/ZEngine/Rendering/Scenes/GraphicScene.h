#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Lights/Light.h>
#include <Rendering/Meshes/Mesh.h>
#include <Textures/Texture.h>
#include <ZEngineDef.h>
#include <entt/entt.hpp>
#include <uuid.h>
#include <future>
#include <mutex>
#include <set>
#include <vector>

namespace ZEngine::Serializers
{
    class GraphicScene3DSerializer;
}

namespace ZEngine::Rendering::Renderers
{
    struct AsyncResourceLoader;
    struct GraphicRenderer;
    class RenderGraph;
} // namespace ZEngine::Rendering::Renderers

namespace ZEngine::Rendering::Scenes
{
    /*
     * This internal defragmented storage represents SceneNode struct with a DoD (Data-Oriented Design) approach
     * The access is index based.
     *
     *  (1)
     *  /
     * (2) --> (3) --> (4) --> (5) --> ##-1
     *         /
     *        (6) --> ##-1
     */
    struct SceneNodeHierarchy
    {
        int Parent       = -1;
        int FirstChild   = -1;
        int RightSibling = -1;
        int DepthLevel   = -1;
    };

    struct DrawData
    {
        uint32_t TransformIndex = std::numeric_limits<uint32_t>::max();
        uint32_t MaterialIndex  = std::numeric_limits<uint32_t>::max();
        uint32_t VertexOffset   = std::numeric_limits<uint32_t>::max();
        uint32_t IndexOffset    = std::numeric_limits<uint32_t>::max();
        uint32_t VertexCount    = std::numeric_limits<uint32_t>::max();
        uint32_t IndexCount     = std::numeric_limits<uint32_t>::max();
    };

    struct SceneRawData : public Helpers::RefCounted
    {
        uint32_t                                   SVertexDataSize              = 0;
        uint32_t                                   SIndexDataSize               = 0;
        uint32_t                                   SMeshCountOffset             = 0;
        std::vector<SceneNodeHierarchy>            NodeHierarchies              = {};
        std::vector<glm::mat4>                     LocalTransforms              = {};
        std::vector<glm::mat4>                     GlobalTransforms             = {};
        std::map<uint32_t, std::set<uint32_t>>     LevelSceneNodeChangedMap     = {};
        /*
         * New Properties
         */
        std::vector<float>                         Vertices                     = {};
        std::vector<uint32_t>                      Indices                      = {};
        std::vector<DrawData>                      DrawData                     = {};
        std::vector<std::string>                   Names                        = {};
        std::vector<std::string>                   MaterialNames                = {};
        std::unordered_map<uint32_t, uint32_t>     NodeMeshes                   = {};
        std::unordered_map<uint32_t, uint32_t>     NodeNames                    = {};
        std::unordered_map<uint32_t, uint32_t>     NodeMaterials                = {};
        std::unordered_map<uint32_t, entt::entity> NodeEntities                 = {};
        std::vector<Meshes::MeshVNext>             Meshes                       = {};
        std::vector<Meshes::MeshMaterial>          Materials                    = {};
        std::vector<Meshes::MaterialFile>          MaterialFiles                = {};

        /*
         * Scene Entity Related data
         */
        std::vector<Lights::GpuDirectionLight>     DirectionalLights            = {};
        std::vector<Lights::GpuPointLight>         PointLights                  = {};
        std::vector<Lights::GpuSpotlight>          SpotLights                   = {};

        /*
         * Buffers
         */
        Hardwares::StorageBufferSetHandle          TransformBufferHandle        = {};
        Hardwares::StorageBufferSetHandle          VertexBufferHandle           = {};
        Hardwares::StorageBufferSetHandle          IndexBufferHandle            = {};
        Hardwares::StorageBufferSetHandle          MaterialBufferHandle         = {};
        Hardwares::StorageBufferSetHandle          IndirectDataDrawBufferHandle = {};
        Hardwares::IndirectBufferSetHandle         IndirectBufferHandle         = {};

        int                                        AddNode(int parent, int depth);
        bool                                       SetNodeName(int node_id, std::string_view name);
    };

    entt::registry& GetEntityRegistry();

    struct SceneEntity : public Helpers::RefCounted
    {
        SceneEntity() = default;
        SceneEntity(int node, Helpers::WeakRef<Scenes::SceneRawData> scene) : m_node(node), m_weak_scene(scene) {}
        ~SceneEntity() = default;

        void             SetName(std::string_view name);
        void             SetTransform(glm::mat4 transform);
        std::string_view GetName() const;
        glm::mat4        GetTransform() const;
        int              GetNode() const;

        template <typename TComponent>
        bool HasComponent() const
        {
            if (auto scene = m_weak_scene.lock())
            {
                if (!scene->NodeEntities.contains(m_node))
                {
                    return false;
                }

                entt::entity entity = scene->NodeEntities[m_node];
                return GetEntityRegistry().all_of<TComponent>(entity);
            }
            return false;
        }

        template <typename TComponent>
        TComponent& GetComponent() const
        {
            auto         scene  = m_weak_scene.lock();
            entt::entity entity = scene->NodeEntities[m_node];
            return GetEntityRegistry().get<TComponent>(entity);
        }

        template <typename TComponent, typename... Args>
        TComponent& AddComponent(Args&&... args)
        {
            if (HasComponent<TComponent>())
            {
                ZENGINE_CORE_WARN("This component has already been added to this entity")
                return GetComponent<TComponent>();
            }

            auto  scene                 = m_weak_scene.lock();
            auto& registry              = GetEntityRegistry();
            auto  entity                = registry.create();
            scene->NodeEntities[m_node] = entity;
            return registry.emplace<TComponent, Args...>(entity, std::forward<Args>(args)...);
        }

        template <typename TComponent>
        void RemoveComponent()
        {
            if (auto scene = m_weak_scene.lock())
            {
                entt::entity entity = scene->NodeEntities[m_node];
                GetEntityRegistry().remove<TComponent>(entity);
            }
        }

    private:
        int                                    m_node{-1};
        Helpers::WeakRef<Scenes::SceneRawData> m_weak_scene;
    };

    struct GraphicScene : public Helpers::RefCounted
    {
        GraphicScene();

        bool                           IsDrawDataDirty = false;
        Helpers::Ref<SceneRawData>     SceneData       = nullptr;

        void                           InitOrResetDrawBuffer(Hardwares::VulkanDevice* device, Renderers::RenderGraph* render_graph, Renderers::AsyncResourceLoader* async_loader);

        void                           SetRootNodeName(std::string_view);
        void                           Merge(std::span<SceneRawData> scenes);
        SceneEntity                    GetPrimariyCameraEntity();
        /*
         * SceneEntity operations
         */
        std::future<SceneEntity>       CreateEntityAsync(std::string_view entity_name = "Empty Entity", int parent_id = 0, int depth_level = 1);
        std::future<SceneEntity>       CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name);
        std::future<SceneEntity>       CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name);
        std::future<SceneEntity>       GetEntityAsync(std::string_view entity_name);
        std::future<bool>              RemoveEntityAsync(const SceneEntity& entity);
        /*
         * SceneNode operations
         */
        std::future<bool>              RemoveNodeAsync(int node_identifier);
        int                            GetSceneNodeParent(int node_identifier);
        int                            GetSceneNodeFirstChild(int node_identifier);
        std::vector<int>               GetSceneNodeSiblingCollection(int node_identifier);
        std::string_view               GetSceneNodeName(int node_identifier);
        glm::mat4&                     GetSceneNodeLocalTransform(int node_identifier);
        glm::mat4&                     GetSceneNodeGlobalTransform(int node_identifier);
        const SceneNodeHierarchy&      GetSceneNodeHierarchy(int node_identifier);
        SceneEntity                    GetSceneNodeEntityWrapper(int node_identifier);
        std::future<void>              SetSceneNodeNameAsync(int node_identifier, std::string_view node_name);
        std::future<Meshes::MeshVNext> GetSceneNodeMeshAsync(int node_identifier);
        void                           MarkSceneNodeAsChanged(int node_identifier);
        /*
         * Scene Graph operations
         */
        bool                           HasSceneNodes();
        uint32_t                       GetSceneNodeCount() = delete;
        std::vector<int>               GetRootSceneNodes();
        Helpers::Ref<SceneRawData>     GetRawData();
        void                           ComputeAllTransforms();

        void                           MergeScenes(std::span<SceneRawData> scenes);
        void                           MergeMeshData(std::span<SceneRawData> scenes);
        void                           MergeMaterials(std::span<SceneRawData> scenes);

        template <typename T, typename V>
        static void MergeMap(const std::unordered_map<T, V>& src, std::unordered_map<T, V>& dst, int index_off, int item_off)
        {
            for (const auto& i : src)
            {
                dst[i.first + index_off] = i.second + item_off;
            }
        }

        template <typename T>
        static void MergeVector(std::span<T> src, std::vector<T>& dst)
        {
            dst.insert(std::end(dst), std::cbegin(src), std::cend(src));
        }

    private:
        std::recursive_mutex m_mutex = {};
        friend class ZEngine::Serializers::GraphicScene3DSerializer;
    };
} // namespace ZEngine::Rendering::Scenes

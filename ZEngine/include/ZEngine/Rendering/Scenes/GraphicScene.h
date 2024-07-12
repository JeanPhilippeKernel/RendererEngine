#pragma once
#include <vector>
#include <future>
#include <set>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Lights/Light.h>
#include <entt/entt.hpp>
#include <uuid.h>
#include <mutex>
#include <ZEngineDef.h>

namespace ZEngine::Serializers
{
    class GraphicScene3DSerializer;
}

namespace ZEngine::Rendering::Scenes
{
    struct SceneNodeHierarchy
    {
        int Parent{-1};
        int FirstChild{-1};
        int RightSibling{-1};
        int DepthLevel{-1};
    };

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
    struct SceneRawData : public Helpers::RefCounted
    {
        uint32_t                               SVertexOffset{0};
        uint32_t                               SIndexOffset{0};
        std::vector<SceneNodeHierarchy>        NodeHierarchyCollection;
        std::vector<glm::mat4>                 LocalTransformCollection;
        std::vector<glm::mat4>                 GlobalTransformCollection;
        std::map<uint32_t, std::set<uint32_t>> LevelSceneNodeChangedMap;
        std::set<int>                          TextureCollection;
        /*
         * New Properties
         */
        std::vector<float>                         Vertices;
        std::vector<uint32_t>                      Indices;
        std::vector<std::string>                   Names;
        std::vector<std::string>                   MaterialNames;
        std::unordered_map<uint32_t, uint32_t>     NodeMeshes;
        std::unordered_map<uint32_t, uint32_t>     NodeNames;
        std::unordered_map<uint32_t, uint32_t>     NodeMaterials;
        std::unordered_map<uint32_t, entt::entity> NodeEntities;
        std::vector<Meshes::MeshVNext>             Meshes    = {};
        std::vector<Meshes::MeshMaterial>          Materials = {};
        std::vector<std::string>                   Files     = {};
        /*
         * Scene Entity Related data
         */
        std::vector<Lights::GpuDirectionLight> DirectionalLights;
        std::vector<Lights::GpuPointLight>     PointLights;
        std::vector<Lights::GpuSpotlight>      SpotLights;

        static int  AddNode(ZEngine::Rendering::Scenes::SceneRawData*, int parent, int depth);
        static bool SetNodeName(ZEngine::Rendering::Scenes::SceneRawData*, int node_id, std::string_view name);
    };

    entt::registry& GetEntityRegistry();

    struct SceneEntity : public Helpers::RefCounted
    {
        SceneEntity() = default;
        SceneEntity(int node, WeakRef<Scenes::SceneRawData> scene) : m_node(node), m_weak_scene(scene) {}
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
        int                           m_node{-1};
        WeakRef<Scenes::SceneRawData> m_weak_scene;
    };


    struct GraphicScene : public Helpers::RefCounted
    {
        GraphicScene()                    = delete;
        GraphicScene(const GraphicScene&) = delete;
        ~GraphicScene()                   = default;

        static void        Initialize();
        static void        Deinitialize();
        static void        SetRootNodeName(std::string_view);
        static void        Merge(std::span<SceneRawData> scenes);
        static SceneEntity GetPrimariyCameraEntity();
        /*
         * SceneEntity operations
         */
        static std::future<SceneEntity>        CreateEntityAsync(std::string_view entity_name = "Empty Entity", int parent_id = 0, int depth_level = 1);
        static std::future<SceneEntity>        CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name);
        static std::future<SceneEntity>        CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name);
        static std::future<SceneEntity>        GetEntityAsync(std::string_view entity_name);
        static std::future<bool>               RemoveEntityAsync(const SceneEntity& entity);
        /*
         * SceneNode operations
         */
        static std::future<bool>              RemoveNodeAsync(int node_identifier);
        static int                            GetSceneNodeParent(int node_identifier);
        static int                            GetSceneNodeFirstChild(int node_identifier);
        static std::vector<int>               GetSceneNodeSiblingCollection(int node_identifier);
        static std::string_view               GetSceneNodeName(int node_identifier);
        static glm::mat4&                     GetSceneNodeLocalTransform(int node_identifier);
        static glm::mat4&                     GetSceneNodeGlobalTransform(int node_identifier);
        static const SceneNodeHierarchy&      GetSceneNodeHierarchy(int node_identifier);
        static SceneEntity                    GetSceneNodeEntityWrapper(int node_identifier);
        static std::future<void>              SetSceneNodeNameAsync(int node_identifier, std::string_view node_name);
        static std::future<Meshes::MeshVNext> GetSceneNodeMeshAsync(int node_identifier);
        static void                           MarkSceneNodeAsChanged(int node_identifier);
        /*
         * Scene Graph operations
         */
        static bool              HasSceneNodes();
        static uint32_t          GetSceneNodeCount() = delete;
        static std::vector<int>  GetRootSceneNodes();
        static Ref<SceneRawData> GetRawData();
        static void              SetRawData(Ref<SceneRawData>&& data);
        static void              ComputeAllTransforms();

    private:
        static Ref<SceneRawData>    s_raw_data;
        static Ref<entt::registry>  s_entities;
        static std::recursive_mutex s_scene_node_mutex;
        friend class ZEngine::Serializers::GraphicScene3DSerializer;

    private:
        static void MergeScenes(std::span<SceneRawData> scenes);
        static void MergeMeshData(std::span<SceneRawData> scenes);
        static void MergeMaterials(std::span<SceneRawData> scenes);

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
    };
} // namespace ZEngine::Rendering::Scenes

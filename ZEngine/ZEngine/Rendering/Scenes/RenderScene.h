#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>

namespace ZEngine::Rendering
{
    class RenderResourceManager;
}

namespace ZEngine::Rendering::Scenes
{
    struct GridConfig
    {
        bool  Enabled       = true;
        float CellSize      = 0.025f;
        float FadeRadius    = 500.0f;
        float FadeStrength  = 0.5f;
        float LineWidth     = 1.5f;
        int   MaxLOD        = 5;
        float GroundY       = 0.0f;
        float ColorThin[4]  = {0.6f, 0.6f, 0.6f, 1.0f};
        float ColorThick[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float ColorXAxis[4] = {0.9f, 0.2f, 0.2f, 1.0f};
        float ColorZAxis[4] = {0.2f, 0.4f, 1.0f, 1.0f};
    };

    // Sky rendering configuration — stored per scene, serialized in .zescene.
    // Supported modes: "atmosphere" (default), "hdri", "skySphere".
    struct SkyConfig
    {
        ZEngine::Core::Containers::String Mode           = {}; // "atmosphere", "hdri", "skySphere"
        ZEngine::Core::Containers::String EnvironmentMap = {}; // .zenvmap filename (hdri mode only)

        bool                              IsHDRI() const
        {
            return Mode.c_str() && (strcmp(Mode.c_str(), "hdri") == 0);
        }
        bool IsAtmosphere() const
        {
            return Mode.empty() || (strcmp(Mode.c_str(), "atmosphere") == 0);
        }
        bool IsSkySphere() const
        {
            return Mode.c_str() && (strcmp(Mode.c_str(), "skySphere") == 0);
        }
    };

    struct SceneData
    {
        // Camera UBO — migrated to PerFrameUploadHeap; offset updated each frame in DrawScene
        uint32_t                  CameraHeapOffset                  = 0;

        // Indirect draw commands — migrated to PerFrameUploadHeap each frame.
        // The heap is reset every frame so we cache the commands here and re-push every frame.
        uint32_t                  IndirectHeapOffset                = 0;
        uint32_t                  IndirectCommandCount              = 0;
        static constexpr uint32_t MAX_DRAW_COMMANDS                 = 512;
        VkDrawIndirectCommand     CachedDrawCmds[MAX_DRAW_COMMANDS] = {};

        // RMM-owned HOST_VISIBLE buffers — written via RRM::UpdateBuffer every frame.
        Core::Memory::BufferView  TransformBuffer                   = {};
        Core::Memory::BufferView  MaterialBuffer                    = {};
        Core::Memory::BufferView  RenderDataBuffer                  = {};

        // RRM vertex buffer handle — index buffer is paired via RRM::GetIndexBuffer(RMMVertexHandle).
        Rendering::BufferHandle   RMMVertexHandle                   = {};
    };
    ZDEFINE_PTR(SceneData);

    // One placed instance of a mesh asset in the scene.
    // Every drag-drop creates a new MeshInstance — even the same mesh dropped
    // twice produces two independent instances with separate transforms.
    struct MeshInstance
    {
        uint32_t           Id        = 0;
        uuids::uuid        MeshUUID  = {};
        Core::Maths::Mat4f Transform = {};
        char               Name[128] = {};
    };

    // Seqlock-protected scene state.
    //   Main thread:   Add/Remove/SetTransform + MarkInstancesDirty
    //   Render thread: GetInstancesSnapshot (spin-wait when seq is odd)
    //
    // Sequence counter convention:
    //   even → data stable (safe to snapshot)
    //   odd  → write in progress (reader spins)
    //
    // Arena allocators never free: even a "torn" pointer to Instances.m_data
    // points at still-valid memory, so retry-on-mismatch is safe.
    struct RenderScene
    {
        // Seqlock counter — even = stable, odd = main-thread writing.
        PaddedAtomic<uint64_t>                m_seq              = {};

        Core::Containers::Array<MeshInstance> Instances          = {};
        Core::Memory::ArenaAllocator          InstanceArena      = {};
        uint32_t                              NextInstanceId     = 1;
        PaddedAtomic<int32_t>                 SelectedInstanceId = {};

        // Set by main thread after every add/remove/transform-change.
        PaddedAtomic<bool>                    InstancesDirty[3]  = {};

        PaddedAtomic<bool>                    SkyDirty[3]        = {};
        SkyConfig                             Sky                = {};

        PaddedAtomic<bool>                    GridDirty[3]       = {};
        GridConfig                            Grid               = {};

        uint32_t                              AddMeshInstance(const uuids::uuid& uuid, const char* name);
        void                                  RemoveMeshInstance(uint32_t id, ZEngine::Rendering::RenderResourceManager* rrm = nullptr);
        void                                  SetInstanceTransform(uint32_t id, const Core::Maths::Mat4f& t);
        void                                  MarkInstancesDirty();

        // Fills `out` with a consistent copy; retries if a write was in progress.
        void                                  GetInstancesSnapshot(Core::Memory::ArenaAllocator* scratch, Core::Containers::Array<MeshInstance>& out) const;

    protected:
        void SeqBeginWrite();
        void SeqEndWrite();
    };
    ZDEFINE_PTR(RenderScene);

// GraphicScene, SceneRawData, SceneNodeHierarchy, SceneEntity removed.
// The ECS::Scene (sparse-set entity store) and EditorScene (RenderScene extension)
// replaced the old entity-graph approach.
#if 0
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
        std::vector<Core::Maths::Mat4f>                     LocalTransforms              = {};
        std::vector<Core::Maths::Mat4f>                     GlobalTransforms             = {};
        std::map<uint32_t, std::set<uint32_t>>     LevelSceneNodeChangedMap     = {};
        /*
         * New Properties
         */
        std::vector<float>                         Vertices                     = {};
        std::vector<uint32_t>                      Indices                      = {};
        std::vector<DrawData>                      DrawDataValue                = {};
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
        void             SetTransform(Core::Maths::Mat4f transform);
        std::string_view GetName() const;
        Core::Maths::Mat4f        GetTransform() const;
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
        Core::Maths::Mat4f&                     GetSceneNodeLocalTransform(int node_identifier);
        Core::Maths::Mat4f&                     GetSceneNodeGlobalTransform(int node_identifier);
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
#endif
} // namespace ZEngine::Rendering::Scenes

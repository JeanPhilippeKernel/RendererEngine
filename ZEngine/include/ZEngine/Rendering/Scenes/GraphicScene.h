#pragma once
#include <vector>
#include <future>
#include <unordered_set>
#include <set>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <entt/entt.hpp>
#include <uuid.h>
#include <mutex>
#include <assimp/scene.h>
#include <ZEngineDef.h>
#include <Rendering/Textures/Texture.h>

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
        std::map<uint32_t, entt::entity>       SceneNodeEntityMap;
        std::map<uint32_t, std::set<uint32_t>> LevelSceneNodeChangedMap;
        std::set<int>                          TextureCollection;
        std::shared_ptr<entt::registry>        EntityRegistry;
        /*
         * New Properties
         */
        std::vector<float>                     Vertices;
        std::vector<uint32_t>                  Indices;
        std::vector<std::string>               Names;
        std::vector<std::string>               MaterialNames;
        std::unordered_map<uint32_t, uint32_t> NodeMeshes;
        std::unordered_map<uint32_t, uint32_t> NodeNames;
        std::unordered_map<uint32_t, uint32_t> NodeMaterials;
        std::vector<Meshes::MeshVNext>         Meshes    = {};
        std::vector<Meshes::MeshMaterial>      Materials = {};
        std::vector<std::string>               Files     = {};
    };

    struct GraphicScene : public Helpers::RefCounted
    {
        GraphicScene()                    = delete;
        GraphicScene(const GraphicScene&) = delete;
        ~GraphicScene()                   = default;

        static void                         Initialize();
        static void                         Deinitialize();
        static Entities::GraphicSceneEntity GetPrimariyCameraEntity();
        /*
         * SceneEntity operations
         */
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(std::string_view entity_name = "Empty Entity");
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(uuids::uuid uuid, std::string_view entity_name);
        static std::future<Entities::GraphicSceneEntity> CreateEntityAsync(std::string_view uuid_string, std::string_view entity_name);
        static std::future<Entities::GraphicSceneEntity> GetEntityAsync(std::string_view entity_name);
        static std::future<bool>                         RemoveEntityAsync(const Entities::GraphicSceneEntity& entity);
        static std::shared_ptr<entt::registry>           GetRegistry();
        /*
         * SceneNode operations
         */
        static std::future<int>               AddNodeAsync(int parent_node, int depth_level);
        static std::future<bool>              RemoveNodeAsync(int node_identifier);
        static int                            GetSceneNodeParent(int node_identifier);
        static int                            GetSceneNodeFirstChild(int node_identifier);
        static std::vector<int>               GetSceneNodeSiblingCollection(int node_identifier);
        static std::string_view               GetSceneNodeName(int node_identifier);
        static glm::mat4&                     GetSceneNodeLocalTransform(int node_identifier);
        static glm::mat4&                     GetSceneNodeGlobalTransform(int node_identifier);
        static const SceneNodeHierarchy&      GetSceneNodeHierarchy(int node_identifier);
        static Entities::GraphicSceneEntity   GetSceneNodeEntityWrapper(int node_identifier);
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
        static std::recursive_mutex s_scene_node_mutex;
        friend class ZEngine::Serializers::GraphicScene3DSerializer;
    };
} // namespace ZEngine::Rendering::Scenes

﻿#pragma once
#include <vector>
#include <future>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <entt/entt.hpp>
#include <uuid.h>
#include <mutex>
#include <assimp/scene.h>
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
    struct SceneRawData
    {
        std::vector<SceneNodeHierarchy>                     NodeHierarchyCollection;
        std::vector<glm::mat4>                              LocalTransformCollection;
        std::vector<glm::mat4>                              GlobalTransformCollection;
        std::unordered_map<uint32_t, uint32_t>              SceneNodeMaterialMap;
        std::unordered_map<uint32_t, entt::entity>          SceneNodeEntityMap;
        std::unordered_map<uint32_t, std::string>           SceneNodeNameMap;
        std::unordered_map<uint32_t, std::string>           SceneNodeMaterialNameMap;
        std::unordered_map<uint32_t, Meshes::MeshVNext>     SceneNodeMeshMap;
        std::unordered_map<uint32_t, std::vector<uint32_t>> LevelSceneNodeChangedMap;
        Ref<entt::registry>                                 EntityRegistry;
    };

    struct GraphicScene
    {
        GraphicScene()  = delete;
        GraphicScene(const GraphicScene&)  = delete;
        ~GraphicScene() = delete;

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
        static Ref<entt::registry>                       GetRegistry();
        /*
         * SceneNode operations
         */
        static std::future<int32_t>           AddNodeAsync(int parent_node, int depth_level);
        static std::future<bool>              RemoveNodeAsync(int32_t node_identifier);
        static int32_t                        GetSceneNodeParent(int32_t node_identifier);
        static int32_t                        GetSceneNodeFirstChild(int32_t node_identifier);
        static std::vector<int32_t>           GetSceneNodeSiblingCollection(int32_t node_identifier);
        static std::string_view               GetSceneNodeName(int32_t node_identifier);
        static glm::mat4&                     GetSceneNodeLocalTransform(int32_t node_identifier);
        static glm::mat4&                     GetSceneNodeGlobalTransform(int32_t node_identifier);
        static const SceneNodeHierarchy&      GetSceneNodeHierarchy(int32_t node_identifier);
        static Entities::GraphicSceneEntity   GetSceneNodeEntityWrapper(int32_t node_identifier);
        static std::future<void>              SetSceneNodeNameAsync(int32_t node_identifier, std::string_view node_name);
        static std::future<Meshes::MeshVNext> GetSceneNodeMeshAsync(int32_t node_identifier);
        static void                           MarkSceneNodeAsChanged(int32_t node_identifier);
        /*
         * Scene Graph operations
         */
        static bool                 HasSceneNodes();
        static uint32_t             GetSceneNodeCount() = delete;
        static std::vector<int32_t> GetRootSceneNodes();
        static std::future<bool>    ImportAssetAsync(std::string_view asset_filename);
        static std::future<bool>    LoadSceneFilenameAsync(std::string_view scene_file) = delete;
        static Ref<SceneRawData>    GetRawData();
        static void                 ComputeAllTransforms();

    private:
        static Ref<SceneRawData>              s_raw_data;
        static std::recursive_mutex           s_scene_node_mutex;
        static std::future<bool>              __TraverseAssetNodeAsync(const aiScene* assimp_scene, aiNode* node, int parent_node, int depth_level);
        static std::future<Meshes::MeshVNext> __ReadSceneNodeMeshDataAsync(const aiScene* assimp_scene, uint32_t mesh_identifier);
        friend class ZEngine::Serializers::GraphicScene3DSerializer;
    };
} // namespace ZEngine::Rendering::Scenes

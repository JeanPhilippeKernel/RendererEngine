#pragma once
#include <Helpers/NodeHierarchyHelper.h>
#include <Importers/IAssetImporter.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>
#include <ZEngineDef.h>
#include <glm/glm.hpp>
#include <uuid.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace Tetragrama
{
    struct EditorAssetSceneFiles
    {
        Importers::AssetFileType          Type     = Importers::AssetFileType::UNKNOWN;
        uint64_t                          Hash     = {};
        ZEngine::Core::Containers::String Path     = {};
        ZEngine::Core::Containers::String RootPath = {};
    };

    struct EditorSceneNodeHierarchy : public Helpers::NodeHierarchy
    {
        /*
         * NodeRef serves as a mapping between Scene NodeHierarchy & Asset NodeHierarchy
         */
        Importers::AssetNodeRef NodeRef = {};
    };

    struct EditorScene
    {
        std::atomic_bool                                                                            Dirty                    = false;
        std::atomic_bool                                                                            MeshAllocationDirty[3]   = {false, false, false};
        std::atomic_bool                                                                            TransformBufferDirty[3]  = {false, false, false};
        std::atomic_bool                                                                            HasPendingChanges        = false;

        uint32_t                                                                                    CurrentTransformOffset   = 0;
        uint32_t                                                                                    CurrentVertexOffset      = 0;
        uint32_t                                                                                    CurrentIndexOffset       = 0;

        const char*                                                                                 Name                     = "";
        ZEngine::Core::Containers::Array<EditorSceneNodeHierarchy>                                  Hierarchies              = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String>                         Names                    = {};
        ZEngine::Core::Containers::Array<glm::mat4>                                                 LocalTransforms          = {};
        ZEngine::Core::Containers::Array<glm::mat4>                                                 GlobalTransforms         = {};
        ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>                                      NodeNames                = {};

        ZEngine::Core::Containers::Array<float>                                                     Vertices                 = {};
        ZEngine::Core::Containers::Array<uint32_t>                                                  Indices                  = {};
        ZEngine::Core::Containers::HashMap<uuids::uuid, ZEngine::Rendering::Meshes::MeshAllocation> MeshAllocations          = {};
        ZEngine::Core::Containers::HashMap<uint32_t, ZEngine::Rendering::Meshes::SubMeshAllocation> NodeSubMeshesAllocations = {};

        ZEngine::Core::Containers::HashMap<uint64_t, uint32_t>                                      HashToAssetFile          = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>                                     AssetFiles               = {};

        ZEngine::Core::Memory::ArenaAllocator                                                       LocalArena               = {};

        ZEngine::Hardwares::VulkanDevice*                                                           Device                   = nullptr;

        void                                                                                        Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Hardwares::VulkanDevice* device, const char* scene_name = "");

        bool                                                                                        HasPendingChange() const;

        int                                                                                         AddHierarchyNode(int parent, int depth);

        int                                                                                         CreateSceneNode(int parent = 0, int depth = 1, const Importers::AssetNodeRef& = {});
        void                                                                                        RemoveSceneNode(int node_id);
        void                                                                                        ReparentNode(int node_id, int new_parent);
        bool                                                                                        IsSceneNodeDeleted(int node_id);

        const ZEngine::Rendering::Meshes::MeshAllocation&                                           CreateOrGetMeshAllocation(Importers::AssetMesh* const);

        void                                                                                        PushAssetFile(const Importers::AssetImporterOutput&);

        void                                                                                        MarkDirty(bool value);
        bool                                                                                        IsDirty();

        void                                                                                        Reset();
        void                                                                                        InitRootNode();

        void                                                                                        ExtractAsync(const EditorScene& scene);
    };

} // namespace Tetragrama
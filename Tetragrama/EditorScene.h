#pragma once
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace Tetragrama
{
    struct EditorAssetSceneFiles
    {
        ZEngine::Importers::AssetFileType Type     = ZEngine::Importers::AssetFileType::UNKNOWN;
        uint64_t                          Hash     = {};
        ZEngine::Core::Containers::String Path     = {};
        ZEngine::Core::Containers::String RootPath = {};
    };

    struct EditorScene : public ZEngine::Rendering::Scenes::RenderScene
    {
        std::atomic_bool                                                                         Dirty                    = false;
        std::atomic_bool                                                                         HasPendingChanges        = false;

        cstring                                                                                  Name                     = "";
        std::atomic_int                                                                          SelectedSceneNode        = -1;
        ZEngine::Core::Containers::Array<ZEngine::Importers::AssetNodeRef>                       HierarchiesNodeRef       = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String>                      Names                    = {};
        ZEngine::Core::Containers::UnorderedHashMap<uint32_t, uint32_t>                          NodeNames                = {};

        ZEngine::Helpers::Ref<ZEngine::Helpers::ThreadSafeQueue<ZEngine::Managers::AssetHandle>> PendingOnLoadHierarchies = nullptr;
        ZEngine::Core::Containers::UnorderedHashMap<uint64_t, uint32_t>                          HashToAssetFile          = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>                                  AssetFiles               = {};

        ZEngine::Core::Memory::ArenaAllocator                                                    LocalArena               = {};

        void                                                                                     Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring scene_name = "");

        bool                                                                                     HasPendingChange() const;

        int                                                                                      AddHierarchyNode(int parent, int depth);

        int                                                                                      CreateSceneNode(int parent = 0, int depth = 1, const ZEngine::Importers::AssetNodeRef& = {});
        void                                                                                     RemoveSceneNode(int node_id);
        void                                                                                     ReparentNode(int node_id, int new_parent);
        bool                                                                                     IsSceneNodeDeleted(int node_id);

        const ZEngine::Rendering::Meshes::MeshAllocation&                                        CreateOrGetMeshAllocation(ZEngine::Importers::AssetMesh* const);

        void                                                                                     PushAssetFile(const ZEngine::Importers::AssetImporterOutput&);

        void                                                                                     MarkDirty(bool value);
        bool                                                                                     IsDirty();

        void                                                                                     Reset();
        void                                                                                     InitRootNode();

        void                                                                                     ExtractAsync(const EditorScene& scene);
    };
    ZDEFINE_PTR(EditorScene);

} // namespace Tetragrama
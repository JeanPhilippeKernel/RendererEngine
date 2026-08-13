#pragma once
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

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
        cstring                                                         Name              = "";
        PaddedAtomic<bool>                                              Dirty             = {};
        PaddedAtomic<bool>                                              HasPendingChanges = {};

        ZEngine::Core::Containers::UnorderedHashMap<uint64_t, uint32_t> HashToAssetFile   = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>         AssetFiles        = {};

        ZEngine::Core::Memory::ArenaAllocator                           LocalArena        = {};

        void                                                            Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring scene_name = "");

        bool                                                            HasPendingChange() const;
        void                                                            PushAssetFile(const ZEngine::Importers::AssetImporterOutput&);
        void                                                            MarkDirty(bool value);
        bool                                                            IsDirty();
        void                                                            Reset();
        void                                                            ExtractAsync(const EditorScene& scene);
    };
    ZDEFINE_PTR(EditorScene);

} // namespace Tetragrama
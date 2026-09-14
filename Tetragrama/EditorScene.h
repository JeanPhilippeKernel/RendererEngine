#pragma once
#include <ZEngine/ECS/ActorManager.h>
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
        cstring                                                         Name                = "";
        PaddedAtomic<bool>                                              Dirty               = {};
        PaddedAtomic<bool>                                              HasPendingChanges   = {};
        ZEngine::ECS::ActorHandle                                       SelectedActorHandle = {}; // invalid = nothing selected

        ZEngine::Core::Containers::UnorderedHashMap<uint64_t, uint32_t> HashToAssetFile     = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>         AssetFiles          = {};

        ZEngine::Core::Memory::ArenaAllocator                           LocalArena          = {};

        ~EditorScene();

        void                      Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring scene_name = "", const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        /// @brief Initializes persistent scene storage without creating default editor content.
        bool                      InitializeDeserialized(size_t page_size);

        bool                      HasPendingChange() const;
        void                      PushAssetFile(const ZEngine::Importers::AssetImporterOutput&);
        void                      MarkDirty(bool value);
        bool                      IsDirty();
        void                      Reset(const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        void                      ExtractAsync(const EditorScene& scene);

        /// @brief Assigns a directional actor as the sky's optional sun source.
        /// @return False when @p handle is stale or does not name a directional light.
        bool                      SetPrimaryCelestialLight(ZEngine::ECS::ActorHandle handle);
        void                      ClearPrimaryCelestialLight();

        // Create a fully wired Actor: registers with RenderScene and adds
        // NameComponent + TransformComponent + MeshComponent in one call.
        // Returns the ActorHandle (invalid if ActorManager is not live).
        ZEngine::ECS::ActorHandle SpawnMeshActor(const uuids::uuid& mesh_uuid, const char* name);
    };
    ZDEFINE_PTR(EditorScene);

} // namespace Tetragrama

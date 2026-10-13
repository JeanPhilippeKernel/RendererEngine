#pragma once
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Tools/TransformEditSession.h>
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
        // Compatibility primary selection for existing inspector/camera panels. The
        // TransformSession is authoritative and can represent a multi-selection.
        ZEngine::ECS::ActorHandle                                       SelectedActorHandle = {};
        ZEngine::ECS::Tools::TransformEditSession                       TransformSession    = {};

        ZEngine::Core::Containers::UnorderedHashMap<uint64_t, uint32_t> HashToAssetFile     = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>         AssetFiles          = {};

        ZEngine::Core::Memory::ArenaAllocator                           LocalArenaStorage   = {};
        ZEngine::Core::Memory::ArenaAllocator*                          LocalArena          = nullptr;
        PaddedAtomic<bool>*                                             DeserializedInUse   = nullptr;

        ~EditorScene();

        void                      Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring scene_name = "", const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        /// @brief Initializes persistent scene storage without creating default editor content.
        bool                      InitializeDeserialized();

        bool                      HasPendingChange() const;
        void                      PushAssetFile(const ZEngine::Importers::AssetImporterOutput&);
        void                      MarkDirty(bool value);
        bool                      IsDirty();
        void                      Reset(const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        void                      ExtractAsync(const EditorScene& scene);

        /// @brief Selection entry points used by hierarchy, viewport and shortcuts.
        /// @details They keep the legacy primary ActorHandle synchronized with the
        ///          transform session's ECS selection.
        bool                      SelectActor(ZEngine::ECS::ActorHandle handle, bool append = false);
        /// @brief Selects the actor that owns a render-scene instance identifier.
        bool                      SelectRenderInstance(ZEngine::ECS::Scene& scene, uint32_t instance_id, bool toggle = false);
        bool                      ToggleActorSelection(ZEngine::ECS::ActorHandle handle);
        bool                      RemoveActorFromSelection(ZEngine::ECS::ActorHandle handle);
        bool                      EnsureActorUUID(ZEngine::ECS::ActorHandle handle);
        void                      ClearSelection();
        [[nodiscard]] bool        IsActorSelected(ZEngine::ECS::ActorHandle handle) const;
        [[nodiscard]] uint32_t    SelectedActorCount() const;

        [[nodiscard]] bool        UndoTransform();
        [[nodiscard]] bool        RedoTransform();
        [[nodiscard]] bool        CanUndoTransform() const;
        [[nodiscard]] bool        CanRedoTransform() const;

        /// @brief Assigns a directional actor as the sky's optional sun source.
        /// @return False when @p handle is stale or does not name a directional light.
        bool                      SetPrimaryCelestialLight(ZEngine::ECS::ActorHandle handle);
        void                      ClearPrimaryCelestialLight();

        // Create a fully wired Actor: registers with RenderScene and adds
        // NameComponent + TransformComponent + MeshComponent in one call.
        // Returns the ActorHandle (invalid if ActorManager is not live).
        ZEngine::ECS::ActorHandle SpawnMeshActor(const uuids::uuid& mesh_uuid, const char* name);

    private:
        void SyncPrimarySelection();
        void ReleaseDeserializedArena();
    };
    ZDEFINE_PTR(EditorScene);

} // namespace Tetragrama

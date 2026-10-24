#pragma once
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace ZEngine
{
    struct EngineContext;
}

namespace Tetragrama
{
    class EditorSession;
    struct EditorAssetSceneFiles
    {
        ZEngine::Importers::AssetFileType Type     = ZEngine::Importers::AssetFileType::UNKNOWN;
        uint64_t                          Hash     = {};
        ZEngine::Core::Containers::String Path     = {};
        ZEngine::Core::Containers::String RootPath = {};
    };

    struct EditorScene : public ZEngine::Rendering::Scenes::RenderScene
    {
        EditorScene()                                                                       = default;

        cstring                                                         Name                = "";
        uuids::uuid                                                     UUID                = {};
        ZEngine::ECS::SceneGridSettings                                 AuthoredGrid        = {};
        ZEngine::Core::VFS::VFSPath                                     SourcePath          = {};
        bool                                                            SourceReadOnly      = false;
        PaddedAtomic<bool>                                              Dirty               = {};
        PaddedAtomic<bool>                                              HasPendingChanges   = {};
        // Compatibility state for standalone/unmigrated scene callers only. When
        // Session is attached, it is never the selection owner or read by the UI.
        ZEngine::ECS::ActorHandle                                       SelectedActorHandle = {};
        EditorSession*                                                  Session             = nullptr; // non-owning; Editor owns lifetime

        ZEngine::Core::Containers::UnorderedHashMap<uint64_t, uint32_t> HashToAssetFile     = {};
        ZEngine::Core::Containers::Array<EditorAssetSceneFiles>         AssetFiles          = {};

        ZEngine::Core::Memory::ArenaAllocator                           LocalArenaStorage   = {};
        ZEngine::Core::Memory::ArenaAllocator*                          LocalArena          = nullptr;
        PaddedAtomic<bool>*                                             DeserializedInUse   = nullptr;

        ~EditorScene();

        void                   Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring scene_name = "", const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        /// @brief Initializes persistent scene storage without creating default editor content.
        bool                   InitializeDeserialized();
        /// @brief Injectable bounded load-slot owner; never creates default entities.
        bool                   InitializeDeserialized(ZEngine::EngineContext& context);

        bool                   HasPendingChange() const;
        void                   PushAssetFile(const ZEngine::Importers::AssetImporterOutput&);
        void                   MarkDirty(bool value);
        /// Monotonic revision for legacy authored edits outside session history.
        [[nodiscard]] uint64_t ExternalRevision() const
        {
            return m_external_revision.value.load(std::memory_order_acquire);
        }
        bool                                 IsDirty();
        /// Derived compatibility adapter; authored Grid is the persistence owner.
        void                                 SyncGridFromAuthored();
        [[nodiscard]] ZEngine::ECS::EntityID SelectedEntity(const ZEngine::ECS::ActorManager* actors = nullptr) const;
        /// Scene-owned focus target, with no entity/actor or infinite-plane bound.
        /// Preserves output unless a live explicitly selected Grid resolves.
        [[nodiscard]] bool                   TryGetSelectedGridOrigin(ZEngine::Core::Maths::Vec3f& output) const;
        [[nodiscard]] bool                   IsSelected(ZEngine::ECS::EntityID entity, const ZEngine::ECS::ActorManager* actors = nullptr) const;
        [[nodiscard]] bool                   SelectActor(ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle);
        /// @brief Bind scene-owner actor/render adapters at the session mutation point.
        [[nodiscard]] bool                   AttachSession(EditorSession& session, ZEngine::EngineContext& engine);
        /// @brief Standalone compatibility removal. Attached editors must use Session::RequestDelete.
        [[nodiscard]] bool                   DeleteEntity(ZEngine::EngineContext& engine, ZEngine::ECS::EntityID entity);
        void                                 Reset(const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults = {});
        void                                 ExtractAsync(const EditorScene& scene);

        /// @brief Assigns a directional actor as the sky's optional sun source.
        /// @return False when @p handle is stale or does not name a directional light.
        bool                                 SetPrimaryCelestialLight(ZEngine::ECS::ActorHandle handle);
        void                                 ClearPrimaryCelestialLight();

        /// @brief Creates and selects an empty actor, with a fresh identity, and marks the scene dirty.
        ZEngine::ECS::ActorHandle            CreateActor(ZEngine::ECS::ActorManager& actors, const char* name);
        /// @brief Resets local TRS; returns true and marks dirty only when a live actor changes.
        bool                                 ResetActorTransform(ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle);
        /// @brief Reads the hierarchy system's computed world-space position, without traversing parents.
        static bool                          TryGetActorWorldPosition(const ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle, ZEngine::Core::Maths::Vec3f& position);

        // Create a fully wired Actor: registers with RenderScene and adds
        // UUID + Name + Transform + Mesh in the exclusive session mutation phase.
        // This untracked structural edit restores previews and invalidates history.
        [[nodiscard]] bool                   CanSpawnMeshActor(const ZEngine::EngineContext& engine) const;
        ZEngine::ECS::ActorHandle            SpawnMeshActor(ZEngine::EngineContext& engine, const uuids::uuid& mesh_uuid, const char* name);

    private:
        PaddedAtomic<uint64_t>  m_external_revision = {};
        ZEngine::EngineContext* SessionEngine       = nullptr;
        bool                    DeleteEntityAtMutationPoint(ZEngine::EngineContext& engine, ZEngine::ECS::EntityID entity);
        void                    ReleaseDeserializedArena();
    };
    ZDEFINE_PTR(EditorScene);

} // namespace Tetragrama

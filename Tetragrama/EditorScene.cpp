#include <Tetragrama/EditorScene.h>
#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <random>
using namespace ZEngine::Core::Containers;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Managers;
using ZEngine::Core::VFS::VFSPath;

namespace Tetragrama
{
    namespace
    {
        uuids::uuid GenerateEntityUUID()
        {
            std::random_device           random_device{};
            std::mt19937                 generator(random_device());
            uuids::uuid_random_generator uuid_generator(generator);
            return uuid_generator();
        }
    } // namespace

    EditorScene::~EditorScene()
    {
        // InstanceArena is carved from LocalArena. Tear it down while its
        // parent is still alive; base-class destruction happens afterwards.
        InstanceArena.Shutdown();
        ReleaseDeserializedArena();
    }

    void EditorScene::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring name, const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults)
    {
        // The caller supplies the bounded Editor owner. This child arena covers
        // asset lists, scene graph data, seqlock instance buffers, reload paths,
        // and the 4 MiB InstanceArena sub-arena.
        arena->CreateSubArena(ZMega(200), &LocalArenaStorage, "EditorContext/EditorScene");
        LocalArena = &LocalArenaStorage;

        Name       = name;
        UUID       = GenerateEntityUUID();
        Sky        = sky_defaults;
        Sky.Sanitize();

        AssetFiles.init(LocalArena, 500);
        HashToAssetFile.init(LocalArena, 500);

        // Allocate a sub-arena for the instance list.
        LocalArena->CreateSubArena(ZMega(4), &InstanceArena, "EditorContext/EditorScene/Instances");
        Instances.init(&InstanceArena, 64);

        // Spawn a default directional light so new scenes are not dark.
        // Rotation: -60° pitch (mostly downward), 30° yaw (slight horizontal angle).
        auto* ctx = ZEngine::Engine::GetContext();
        if (ctx && ctx->ActorManager)
        {
            constexpr cstring         default_light_name = "DirectionalLight";

            ZEngine::ECS::ActorHandle handle             = ctx->ActorManager->Create();
            ZEngine::ECS::Actor*      actor              = ctx->ActorManager->Access(handle);
            if (!actor)
            {
                return;
            }

            NameComponent nc = {};
            ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), default_light_name, ZEngine::Helpers::secure_strlen(default_light_name));
            actor->AddComponent<NameComponent>(nc);

            TransformComponent tc = {};
            tc.Rotation.x         = -1.047f;
            tc.Rotation.y         = 0.524f;
            actor->AddComponent<TransformComponent>(tc);

            LightComponent lc = {};
            lc.LightType      = LightComponent::Type::Directional;
            lc.Intensity      = 3.f;
            lc.Color[0]       = 1.f;
            lc.Color[1]       = 1.f;
            lc.Color[2]       = 1.f;
            actor->AddComponent<LightComponent>(lc);

            UUIDComponent uc = {};
            uc.Value         = GenerateEntityUUID();
            actor->AddComponent<UUIDComponent>(uc);
            if (Sky.PrimaryCelestialLight.is_nil())
            {
                Sky.PrimaryCelestialLight = uc.Value;
            }
        }

        MarkSkyDirty();
    }

    bool EditorScene::InitializeDeserialized()
    {
        // A scene must outlive serializer scratch, but it cannot create a new root
        // mapping for every load. Claim one of the two pre-reserved, profiled slots.
        auto* context = ZEngine::Engine::GetContext();
        return context && InitializeDeserialized(*context);
    }

    bool EditorScene::InitializeDeserialized(ZEngine::EngineContext& context)
    {
        // Includes instance storage and the minimum ECS/actor-manager workspace.
        // Reject small/missing slots before legacy container initialization can fail.
        constexpr size_t minimum_slot_bytes = ZMega(8);
        if (LocalArena || DeserializedInUse)
        {
            return false;
        }

        struct SceneLoadSlot
        {
            ZEngine::Core::Memory::ArenaAllocator* Arena = nullptr;
            PaddedAtomic<bool>*                    InUse = nullptr;
        };
        const SceneLoadSlot slots[] = {
            {&context.EditorSceneLoadArenaA, &context.EditorSceneLoadArenaAInUse},
            {&context.EditorSceneLoadArenaB, &context.EditorSceneLoadArenaBInUse},
        };

        for (const SceneLoadSlot& slot : slots)
        {
            bool available = false;
            if (!slot.Arena->m_memory || slot.Arena->m_total_size < minimum_slot_bytes || !slot.InUse->value.compare_exchange_strong(available, true, std::memory_order_acq_rel))
            {
                continue;
            }

            LocalArena        = slot.Arena;
            DeserializedInUse = slot.InUse;
            LocalArena->Clear();
            LocalArena->CreateSubArena(ZMega(4), &InstanceArena, "EditorSceneLoad/Instances");
            if (!InstanceArena.m_memory)
            {
                ReleaseDeserializedArena();
                return false;
            }

            AssetFiles.init(LocalArena, 500);
            HashToAssetFile.init(LocalArena, 500);
            Instances.init(&InstanceArena, 64);
            UUID = GenerateEntityUUID();
            return true;
        }

        ZENGINE_CORE_WARN("[EditorScene] Both bounded deserialization slots are in use")
        return false;
    }

    void EditorScene::ReleaseDeserializedArena()
    {
        if (!DeserializedInUse)
        {
            return;
        }

        LocalArena->Clear();
        DeserializedInUse->value.store(false, std::memory_order_release);
        DeserializedInUse = nullptr;
        LocalArena        = nullptr;
    }

    bool EditorScene::HasPendingChange() const
    {
        return HasPendingChanges.value.load(std::memory_order_acquire);
    }

    void EditorScene::PushAssetFile(const ZEngine::Importers::AssetImporterOutput& data)
    {
        if (data.Type == ZEngine::Importers::AssetFileType::UNKNOWN)
        {
            ZENGINE_CORE_WARN("{} : Invalid operation, unknown asset file type", __FUNCTION__)
            return;
        }

        EditorAssetSceneFiles asset_file = {};
        asset_file.Type                  = data.Type;
        asset_file.Hash                  = ZEngine::Core::Containers::hash_compute(data.Path.c_str());
        asset_file.Path.init(LocalArena, data.Path.c_str());
        asset_file.RootPath.init(LocalArena, data.RootPath.c_str());

        if (HashToAssetFile.contains(asset_file.Hash))
        {
            ZENGINE_CORE_WARN("Asset file already exists at that location : {}", asset_file.Path.c_str())
            return;
        }

        auto index = AssetFiles.size();
        AssetFiles.push(asset_file);
        HashToAssetFile.insert(asset_file.Hash, index);

        HasPendingChanges.value.store(true, std::memory_order_release);
    }

    void EditorScene::MarkDirty(bool value)
    {
        if (value)
        {
            m_external_revision.value.fetch_add(1, std::memory_order_release);
        }
        Dirty.value.store(value, std::memory_order_release);
    }

    void EditorScene::SyncGridFromAuthored()
    {
        if (!ZEngine::Rendering::Scenes::MakeSceneGridRenderSnapshot(AuthoredGrid, Grid))
        {
            // Never retain a drawable stale plane after invalid authored input.
            Grid         = {};
            Grid.Enabled = false;
        }
        for (auto& dirty : GridDirty)
        {
            dirty.value.store(true, std::memory_order_release);
        }
    }

    bool EditorScene::IsDirty()
    {
        return Dirty.value.load(std::memory_order_acquire) || (Session && Session->Dirty());
    }

    ZEngine::ECS::EntityID EditorScene::SelectedEntity(const ZEngine::ECS::ActorManager* actors) const
    {
        if (Session)
        {
            return Session->Resolve(Session->Selection().ActiveUUID());
        }
        const auto* actor = actors ? actors->Access(SelectedActorHandle) : nullptr;
        return actor ? actor->GetEntityID() : ZEngine::ECS::INVALID_ENTITY;
    }

    bool EditorScene::TryGetSelectedGridOrigin(ZEngine::Core::Maths::Vec3f& output) const
    {
        if (!Session || !Session->OnOwnerThread() || Session->Selection().ActiveSceneTarget() != SceneTarget::Grid)
        {
            return false;
        }
        EditorGridView view = {};
        if (!Session->ReadGridSettings(view) || view.SceneUUID != UUID)
        {
            return false;
        }
        output = view.Value.Origin;
        return true;
    }

    bool EditorScene::IsSelected(ZEngine::ECS::EntityID entity, const ZEngine::ECS::ActorManager* actors) const
    {
        if (Session)
        {
            const auto target = Session->TargetFor(entity);
            return !target.UUID.is_nil() && Session->Selection().Contains(target.UUID);
        }
        return entity.IsValid() && entity == SelectedEntity(actors);
    }

    bool EditorScene::SelectActor(ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle)
    {
        const auto* actor = actors.Access(handle);
        if (!actor)
        {
            return false;
        }
        if (Session)
        {
            return Session->RequestSelection(actor->GetEntityID());
        }
        SelectedActorHandle = handle;
        return true;
    }

    bool EditorScene::AttachSession(EditorSession& session, ZEngine::EngineContext& engine)
    {
        if (!engine.Scene || session.Scene() != engine.Scene)
        {
            return false;
        }
        const EditorSceneMutationHooks hooks = {
            .Context = this,
            .Validate =
                [](void* context, const ZEngine::ECS::Scene& ecs, EditorSceneMutation kind, ZEngine::ECS::EntityID entity) {
                    const auto& owner  = *static_cast<EditorScene*>(context);
                    const auto* engine = owner.SessionEngine;
                    if (!engine || engine->Scene != &ecs || !owner.Session || owner.Session->Scene() != &ecs)
                    {
                        return false;
                    }
                    if (kind == EditorSceneMutation::DeleteEntity)
                    {
                        return ecs.IsAlive(entity);
                    }
                    return kind == EditorSceneMutation::CreateCollection && engine->ActorManager && engine->ActorManager->Count() < ZEngine::ECS::ActorManager::MAX_ACTORS && ecs.AliveCount() < ZEngine::ECS::EntityRegistry::MAX_ENTITIES;
                },
            .Apply =
                [](void* context, ZEngine::ECS::Scene&, EditorSceneMutation kind, ZEngine::ECS::EntityID entity, cstring name) {
                    auto& owner = *static_cast<EditorScene*>(context);
                    if (kind == EditorSceneMutation::DeleteEntity)
                    {
                        (void) owner.DeleteEntityAtMutationPoint(*owner.SessionEngine, entity);
                    }
                    else
                    {
                        (void) owner.CreateActor(*owner.SessionEngine->ActorManager, name);
                    }
                },
        };
        const EditorSceneSettingsHooks settings = {
            .Context = this,
            .ReadGrid =
                [](void* context, const ZEngine::ECS::Scene& ecs, EditorGridView& output) {
                    const auto& owner = *static_cast<const EditorScene*>(context);
                    if (!owner.SessionEngine || owner.SessionEngine->Scene != &ecs || !owner.Session || owner.Session->Scene() != &ecs)
                    {
                        return false;
                    }
                    output = {.Value = owner.AuthoredGrid, .SceneUUID = owner.UUID, .Editable = !owner.SourceReadOnly};
                    return true;
                },
            .ApplyGrid =
                [](void* context, ZEngine::ECS::Scene&, const ZEngine::ECS::SceneGridSettings& value) {
                    auto& owner        = *static_cast<EditorScene*>(context);
                    owner.AuthoredGrid = value;
                    owner.SyncGridFromAuthored();
                    // Shared session digest/history owns document dirtiness. Do not
                    // turn undoable Grid edits into sticky legacy external revisions.
                },
        };
        if (!session.SetSceneSettingsHooks(settings) || !session.SetSceneMutationHooks(hooks))
        {
            return false;
        }
        SessionEngine = &engine;
        Session       = &session;
        return true;
    }

    bool EditorScene::DeleteEntity(ZEngine::EngineContext& engine, ZEngine::ECS::EntityID entity)
    {
        return !Session && DeleteEntityAtMutationPoint(engine, entity);
    }

    bool EditorScene::DeleteEntityAtMutationPoint(ZEngine::EngineContext& engine, ZEngine::ECS::EntityID entity)
    {
        auto* ecs = engine.Scene;
        if (!ecs || !ecs->IsAlive(entity) || (Session && (!Session->InMutationPhase() || Session->Scene() != ecs)))
        {
            return false;
        }

        // The session has already restored any preview before this callback.
        // Include pure-ECS children rather than scanning the visible actor rows.
        ecs->ForEach<ParentComponent>([&](auto child, const ParentComponent& link) {
            if (link.Parent == entity)
            {
                (void) ecs->SetParent(child, ZEngine::ECS::INVALID_ENTITY);
            }
        });
        if (const auto* mesh = ecs->GetComponent<MeshComponent>(entity); mesh && mesh->RenderInstanceId != UINT32_MAX)
        {
            RemoveMeshInstance(mesh->RenderInstanceId, engine.RenderResourceManager);
        }
        if (const auto* identity = ecs->GetComponent<UUIDComponent>(entity); identity && identity->Value == Sky.PrimaryCelestialLight)
        {
            ClearPrimaryCelestialLight();
        }

        ZEngine::ECS::ActorHandle handle = {};
        if (engine.ActorManager)
        {
            engine.ActorManager->ForEach([&](auto candidate, const auto* actor) {
                if (actor->GetEntityID() == entity)
                {
                    handle = candidate;
                }
            });
        }
        if (handle.Valid())
        {
            if (!Session && SelectedActorHandle == handle)
            {
                SelectedActorHandle = {};
            }
            engine.ActorManager->Destroy(handle);
        }
        else
        {
            ecs->DestroyEntity(entity);
        }
        MarkDirty(true);
        return true;
    }

    ZEngine::ECS::ActorHandle EditorScene::CreateActor(ZEngine::ECS::ActorManager& actors, const char* name)
    {
        if (!name || actors.Count() >= ZEngine::ECS::ActorManager::MAX_ACTORS)
        {
            return {};
        }
        const auto handle = actors.Create();
        auto*      actor  = actors.Access(handle);
        if (!actor)
        {
            return {};
        }
        NameComponent nc = {};
        ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), name, sizeof(nc.Value) - 1);
        actor->AddComponent<NameComponent>(nc);
        actor->AddComponent<TransformComponent>({});
        UUIDComponent identity = {};
        identity.Value         = GenerateEntityUUID();
        actor->AddComponent<UUIDComponent>(identity);
        if (!SelectActor(actors, handle))
            ZENGINE_CORE_WARN("[Editor] The actor was created but its selection request could not be queued")
        MarkDirty(true);
        return handle;
    }

    bool EditorScene::ResetActorTransform(ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle)
    {
        auto* actor     = actors.Access(handle);
        auto* transform = actor ? actor->GetComponent<TransformComponent>() : nullptr;
        if (!transform)
        {
            return false;
        }
        if (transform->Position.x == 0.f && transform->Position.y == 0.f && transform->Position.z == 0.f && transform->Rotation.x == 0.f && transform->Rotation.y == 0.f && transform->Rotation.z == 0.f && transform->Scale.x == 1.f && transform->Scale.y == 1.f && transform->Scale.z == 1.f)
        {
            return false;
        }
        transform->Position = {};
        transform->Rotation = {};
        transform->Scale    = {1.f, 1.f, 1.f};
        MarkDirty(true);
        return true;
    }

    bool EditorScene::TryGetActorWorldPosition(const ZEngine::ECS::ActorManager& actors, ZEngine::ECS::ActorHandle handle, ZEngine::Core::Maths::Vec3f& position)
    {
        const auto* actor     = actors.Access(handle);
        const auto* transform = actor ? actor->GetComponent<TransformComponent>() : nullptr;
        if (!transform)
        {
            return false;
        }
        position = {transform->WorldTransform(0, 3), transform->WorldTransform(1, 3), transform->WorldTransform(2, 3)};
        return true;
    }

    void EditorScene::Reset(const ZEngine::Rendering::Scenes::SkyConfig& sky_defaults)
    {
        UUID                = GenerateEntityUUID();
        AuthoredGrid        = {};
        SourcePath          = {};
        SourceReadOnly      = false;
        SelectedActorHandle = {};
        HasPendingChanges.value.store(false, std::memory_order_release);
        AssetFiles.clear();
        HashToAssetFile.clear();

        SeqBeginWrite();
        Instances.clear();
        NextInstanceId = 1;
        SeqEndWrite();
        MarkInstancesDirty();

        Grid = {};
        for (auto& dirty : GridDirty)
        {
            dirty.value.store(true, std::memory_order_release);
        }
        CelestialLight = {};
        PendingLights  = ZEngine::Rendering::Scenes::LightArrayUBO{};
        Sky            = sky_defaults;
        Sky.Sanitize();
        MarkSkyDirty();

        Dirty.value.store(false, std::memory_order_release);
    }

    bool EditorScene::SetPrimaryCelestialLight(ZEngine::ECS::ActorHandle handle)
    {
        auto* context = ZEngine::Engine::GetContext();
        if (!context || !context->ActorManager)
        {
            return false;
        }

        ZEngine::ECS::Actor* actor = context->ActorManager->Access(handle);
        if (!actor)
        {
            return false;
        }

        const LightComponent* light = actor->GetComponent<LightComponent>();
        if (!light || light->LightType != LightComponent::Type::Directional)
        {
            return false;
        }

        UUIDComponent* identity = actor->GetComponent<UUIDComponent>();
        if (!identity)
        {
            UUIDComponent created = {};
            created.Value         = GenerateEntityUUID();
            actor->AddComponent<UUIDComponent>(created);
            identity = actor->GetComponent<UUIDComponent>();
        }
        if (!identity || identity->Value.is_nil())
        {
            return false;
        }

        Sky.PrimaryCelestialLight = identity->Value;
        MarkSkyDirty();
        MarkDirty(true);
        return true;
    }

    void EditorScene::ClearPrimaryCelestialLight()
    {
        if (Sky.PrimaryCelestialLight.is_nil())
        {
            return;
        }

        Sky.PrimaryCelestialLight = {};
        MarkSkyDirty();
        MarkDirty(true);
    }

    void EditorScene::ExtractAsync(const EditorScene& scene)
    {
        // Compact the global geometry buffers before ingesting a new scene so
        // orphaned data from the previous scene is reclaimed starting from offset 0.
        auto* ctx = ZEngine::Engine::GetContext();
        if (ctx && ctx->RenderResourceManager)
        {
            ctx->RenderResourceManager->ResetGeometryBuffers();
        }

        for (const auto& file : scene.AssetFiles)
        {
            auto& f = AssetFiles.push_use({});
            f.Hash  = file.Hash;
            f.Type  = file.Type;
            f.Path.init(LocalArena, file.Path.c_str());
            f.RootPath.init(LocalArena, file.RootPath.c_str());
        }

        // Re-ingest cooked assets on scene load.
        // Materials are processed before meshes so their texture handles are available
        // when the mesh submeshes reference them.
        for (const auto& file : AssetFiles)
        {
            if (file.Type == ZEngine::Importers::AssetFileType::MATERIAL)
            {
                ZEngine::Importers::AssetMaterial mat{};
                auto                              path                            = ZEngine::Core::Containers::String{};
                char                              native_buf[MAX_FILE_PATH_COUNT] = {};
                VFSPath::Parse(file.Path.c_str()).Value().ResolveNative(file.RootPath.c_str(), native_buf, sizeof(native_buf));
                path.init(LocalArena, native_buf);
                ZEngine::Importers::AssetCodec::DeserializeMaterialAssetFile(LocalArena, path.c_str(), mat);

                // Reconstruct AssetTexture entries from the inline path fields so
                // IngestTextures can upload them to the GPU.
                ZEngine::Core::Containers::Array<ZEngine::Importers::AssetTexture> textures{};
                textures.init(LocalArena, 5);
                auto add_tex = [&](const uuids::uuid& uuid, const ZEngine::Core::Containers::String& tex_path) {
                    if (!uuid.is_nil() && !tex_path.empty())
                    {
                        auto& t       = textures.push_use({});
                        t.TextureUUID = uuid;
                        t.Path.init(LocalArena, tex_path.c_str());
                    }
                };
                add_tex(mat.AlbedoTexUUID, mat.AlbedoTexPath);
                add_tex(mat.EmissiveTexUUID, mat.EmissiveTexPath);
                add_tex(mat.NormalTexUUID, mat.NormalTexPath);
                add_tex(mat.OpacityTexUUID, mat.OpacityTexPath);
                add_tex(mat.SpecularTexUUID, mat.SpecularTexPath);

                AssetManager::IngestTextures(std::move(textures));
                AssetManager::IngestMaterial(std::move(mat));
            }
        }

        for (const auto& file : AssetFiles)
        {
            if (file.Type == ZEngine::Importers::AssetFileType::MESH)
            {
                ZEngine::Importers::AssetMesh          mesh{};
                ZEngine::Importers::AssetNodeHierarchy hier{};
                auto                                   path                            = ZEngine::Core::Containers::String{};
                char                                   native_buf[MAX_FILE_PATH_COUNT] = {};
                VFSPath::Parse(file.Path.c_str()).Value().ResolveNative(file.RootPath.c_str(), native_buf, sizeof(native_buf));
                path.init(LocalArena, native_buf);
                ZEngine::Importers::AssetCodec::DeserializeMeshAssetFile(LocalArena, path.c_str(), mesh, hier);
                AssetManager::IngestMesh(std::move(mesh), std::move(hier));
            }
        }
    }

    bool EditorScene::CanSpawnMeshActor(const ZEngine::EngineContext& engine) const
    {
        using namespace ZEngine::ECS;
        const auto* world  = engine.Scene;
        const auto* actors = engine.ActorManager;
        if (!Session || !Session->InMutationPhase() || !Session->Editable() || Session->Scene() != world || SourceReadOnly || !world || !actors || actors->BoundScene() != world || actors->Count() >= ActorManager::MAX_ACTORS || world->AliveCount() >= EntityRegistry::MAX_ENTITIES || !NextInstanceId || NextInstanceId == UINT32_MAX)
        {
            return false;
        }
        const auto   available  = [](const ZEngine::Core::Memory::ArenaAllocator* arena) { return arena && arena->m_memory && arena->m_current_offset <= arena->m_total_size ? arena->m_total_size - arena->m_current_offset : size_t{0}; };
        // Includes hierarchy growth, fresh sparse stores/dense grows and conservative
        // storage-map bookkeeping. The actor manager can have a distinct owner.
        const size_t components = world->Hierarchy().ReservationMemoryBound(world->AliveCount() + 1) + world->ComponentAdditionMemoryBound<UUIDComponent>() + world->ComponentAdditionMemoryBound<NameComponent>() + world->ComponentAdditionMemoryBound<TransformComponent>() + world->ComponentAdditionMemoryBound<MeshComponent>() + ZKilo(32);
        const size_t actor      = sizeof(Actor) + alignof(Actor);
        return available(world->GetArena()) >= components + (actors->Arena() == world->GetArena() ? actor : 0) && available(actors->Arena()) >= actor;
    }

    ZEngine::ECS::ActorHandle EditorScene::SpawnMeshActor(ZEngine::EngineContext& engine, const uuids::uuid& mesh_uuid, const char* name)
    {
        size_t length = 0;
        if (name)
        {
            while (length < sizeof(NameComponent::Value) && name[length])
            {
                ++length;
            }
        }
        if (!name || length == sizeof(NameComponent::Value) || mesh_uuid.is_nil() || !CanSpawnMeshActor(engine) || !Instances.try_reserve(Instances.size() + 1))
        {
            return {};
        }
        const auto identity = GenerateEntityUUID();
        if (identity.is_nil())
        {
            return {};
        }
        Session->InvalidateForExternalMutation(); // Restore before any entity/instance is added.
        const auto handle = engine.ActorManager->Create();
        auto*      actor  = engine.ActorManager->Access(handle);
        if (!actor)
        {
            return {};
        }

        NameComponent nc = {};
        ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), name, length);
        actor->AddComponent<NameComponent>(nc);
        actor->AddComponent<TransformComponent>({});
        actor->AddComponent<UUIDComponent>({identity});

        MeshComponent mc    = {};
        mc.MeshUUID         = mesh_uuid;
        mc.RenderInstanceId = AddMeshInstance(mesh_uuid, name);
        actor->AddComponent<MeshComponent>(mc);
        MarkDirty(true);
        return handle;
    }

} // namespace Tetragrama

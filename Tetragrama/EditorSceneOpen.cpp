#include <Tetragrama/Components/ZUI/ZUIRecentScenes.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <Tetragrama/Panels/AssetImporterPanel.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <algorithm>
#include <atomic>
#include <utility>

namespace Tetragrama
{
    namespace
    {
        bool ResolveCachedMesh(void* context, const uuids::uuid& uuid, uint32_t* draws, ZEngine::ECS::SceneDiagnostics*)
        {
            auto* manager = static_cast<ZEngine::Managers::AssetManager*>(context);
            return manager && manager->TryGetMeshDrawCount(uuid, draws);
        }

        bool ProjectSourceStillMatches(cstring workspace, const ZEngine::Core::VFS::VFSPath& path)
        {
            if (!workspace || !workspace[0] || !IsSceneSourcePath(path))
            {
                return false;
            }
            char native[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
            path.ResolveNative(workspace, native, sizeof(native));
            const auto resolved = SceneSourcePathFromNative(workspace, native);
            return resolved.Succeeded() && resolved.Value() == path;
        }

        void LogLoadDiagnostics(const ZEngine::ECS::SceneDiagnostics& diagnostics)
        {
            for (uint32_t i = 0; i < diagnostics.Count; ++i)
            {
                const auto& entry = diagnostics.Entries[i];
                if (entry.Severity == ZEngine::ECS::SceneDiagnosticSeverity::Error)
                {
                    ZENGINE_CORE_ERROR("[Editor] {}", entry.Message)
                }
                else
                {
                    ZENGINE_CORE_WARN("[Editor] {}", entry.Message)
                }
            }
        }
    } // namespace

    struct Editor::SceneOpenWork
    {
        ZEngine::EngineContext*                           EngineOwner = nullptr;
        EditorScene*                                      SceneOwner  = nullptr;
        ZEngine::ECS::Scene*                              World       = nullptr;
        ZEngine::ECS::ActorManager*                       Actors      = nullptr;
        uuids::uuid                                       SceneUUID   = {};
        ZEngine::Core::VFS::IVFSContext*                  VFS         = nullptr;
        const ZEngine::ECS::SceneComponentSchemaRegistry* Schemas     = nullptr;
        ZEngine::Core::VFS::VFSPath                       Path        = {};
        std::string                                       Workspace   = {};
        uint64_t                                          Epoch       = 0;
        ZEngine::ECS::SceneDocument                       Document    = {};
        ZEngine::ECS::SceneDiagnostics                    Diagnostics = {};
        std::atomic<bool>                                 Ready       = false;
        std::atomic<bool>                                 Cancelled   = false;
        bool                                              Succeeded   = false;
        ZEngine::Managers::AssetManager*                  Assets      = nullptr;
        ZEngine::Core::VFS::AssetRegistry*                Registry    = nullptr;

        bool                                              SameWorkspace(cstring current) const
        {
            if (!current)
            {
                return false;
            }
            for (size_t i = 0; i < Workspace.size(); ++i)
            {
                if (Workspace[i] != current[i])
                {
                    return false;
                }
            }
            return current[Workspace.size()] == '\0';
        }

        // Main-thread validation only. The worker never follows these borrowed
        // owner tokens; their services must remain alive until it has retired.
        bool Matches(const Editor& editor, const ZEngine::EngineContext& engine) const
        {
            if (EngineOwner != &engine || SceneOwner != editor.CurrentScene || World != engine.Scene || Actors != engine.ActorManager || VFS != engine.VFS || Schemas != engine.SceneSchemas || !SceneOwner || SceneOwner->UUID != SceneUUID || !editor.WorkingSpacePath)
            {
                return false;
            }
            if (Assets != ZEngine::Managers::AssetManager::Instance() || (Assets && (Assets->Registry != Registry || !SameWorkspace(Assets->CurrentWorkingSpacePath))))
            {
                return false;
            }
            // Compare the captured value, not the char-buffer identity. Stop at
            // the first mismatch (including an early NUL), and never scan beyond
            // the captured length. No allocation/canonicalization on pending polls.
            return SameWorkspace(editor.WorkingSpacePath);
        }

        static void Read(void* context)
        {
            auto& work = *static_cast<SceneOpenWork*>(context);
            {
                if (!work.Cancelled.load(std::memory_order_acquire))
                {
                    if (!ProjectSourceStillMatches(work.Workspace.c_str(), work.Path))
                    {
                        work.Diagnostics.Error("Scene source changed or escaped the project; the active scene was preserved.");
                    }
                    else
                    {
                        work.Succeeded = ZEngine::ECS::SceneDocument::Read(*work.VFS, work.Path, *work.Schemas, &work.Document, &work.Diagnostics);
                        if (work.Succeeded)
                        {
                            work.Succeeded = work.LoadMeshes();
                        }
                    }
                }
            }
            work.Ready.store(true, std::memory_order_release);
        }

        bool LoadMeshes()
        {
            // Immutable semantic references, not a temporary ECS world or source
            // YAML inspection. Reserve once, sort/deduplicate before any asset IO.
            ZEngine::Core::Memory::ArenaAllocator scratch  = {};
            const size_t                          capacity = Document.EntityCount();
            scratch.Initialize(std::max(size_t(1), capacity) * sizeof(uuids::uuid) + 64, 0, "Editor/SceneMeshReferences");
            ZEngine::Core::Containers::Array<uuids::uuid> meshes = {};
            meshes.m_allocator                                   = &scratch;
            if (!meshes.try_reserve(capacity))
            {
                Diagnostics.Error("Cannot retain scene mesh references within their bounded load workspace.");
                return false;
            }
            if (!Document.EnumerateComponentReferences(
                    ZEngine::ECS::ComponentTypeOf<ZEngine::ECS::Components::MeshComponent>(),
                    *Schemas,
                    [](void* context, const ZEngine::ECS::SceneReference& reference) {
                        auto& list = *static_cast<ZEngine::Core::Containers::Array<uuids::uuid>*>(context);
                        if (reference.Kind != ZEngine::ECS::SceneReferenceKind::Asset || reference.UUID.is_nil() || list.size() == list.capacity())
                        {
                            return false;
                        }
                        list.push(reference.UUID);
                        return true;
                    },
                    &meshes,
                    &Diagnostics))
            {
                return false;
            }
            if (meshes.empty())
            {
                return true;
            }
            if (meshes.size() > ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS)
            {
                Diagnostics.Error("Scene mesh instances exceed the complete render budget.");
                return false;
            }
            if (!Assets || !Registry || !SameWorkspace(Assets->CurrentWorkingSpacePath))
            {
                Diagnostics.Error("Scene mesh loading requires the captured asset cache and registry.");
                return false;
            }
            std::sort(meshes.begin(), meshes.end());
            const auto end = std::unique(meshes.begin(), meshes.end());
            meshes.m_size  = size_t(end - meshes.begin());
            for (const auto& uuid : meshes)
            {
                if (Cancelled.load(std::memory_order_acquire))
                {
                    return false;
                }
                const auto loaded = Assets->LoadMeshAsset(*VFS, uuid, {}, &Cancelled);
                if (loaded.Failed())
                {
                    Diagnostics.Error("A referenced mesh failed checked loading; the active scene was preserved.");
                    return false;
                }
            }
            return true;
        }
    };

    Editor::Editor()  = default;
    Editor::~Editor() = default;

    bool Editor::SceneOpenPending() const
    {
        return m_scene_open.Pending || m_scene_open_work != nullptr;
    }

    void Editor::CancelSceneOpen()
    {
        m_scene_change.Reset();
        m_closing            = true;
        m_scene_open.Pending = false;
        m_scene_open_picker_owner.Reset();
        if (m_scene_open_work)
        {
            m_scene_open_work->Cancelled.store(true, std::memory_order_release);
        }
        // Retain the context even if the pool never started this queued task.
        // OnWorldShutdown releases it only after every worker has joined.
    }

    void Editor::OnWorldShutdown(ZEngine::EngineContext& engine)
    {
        CancelSceneOpen();
        MeshDrop.ShutdownAfterWorkersJoined();
        if (AssetImporter)
        {
            AssetImporter->ShutdownAfterWorkersJoined();
        }
        m_scene_open_work.reset();
        Session.Shutdown();
        if (CurrentScene)
        {
            static_cast<EditorScene*>(CurrentScene)->Session = nullptr;
        }
        if (engine.WorldCommands)
        {
            engine.WorldCommands->Clear();
        }
        if (m_loaded_scene)
        {
            m_loaded_scene->Render().Session = nullptr;
            // Actor OnDestroy still sees its original engine world and live services.
            m_loaded_scene.reset();
            engine.Scene        = m_original_world;
            engine.ActorManager = m_original_actors;
            CurrentScene        = nullptr;
        }
        m_original_world  = nullptr;
        m_original_actors = nullptr;
        if (engine.WorldCommands)
        {
            engine.WorldCommands->Clear();
        }
    }

    void Editor::OpenSceneDialog()
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        if (m_closing || !CurrentWindow || !Configuration || !Session.Ready() || m_scene_open_picker.valid() || SceneOpenPending() || m_editor_close_pending || SceneChangePromptState().Visible())
        {
            return;
        }
        std::string directory = {};
        if (!TrySceneDialogDirectory(Configuration->WorkingSpacePath.c_str(), Configuration->ScenePath.c_str(), {}, &directory))
        {
            ZENGINE_CORE_WARN("[Editor] Scene open directory is not a bounded UTF-8 native path")
            return;
        }
        std::string_view filters[] = {".zescene"};
        if (!m_scene_open_picker_owner.Capture(*this))
        {
            return;
        }
        m_scene_open_picker = CurrentWindow->OpenFileDialogAsync(filters, directory, "Select a scene file");
    }

    void Editor::PollSceneOpenDialog()
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        ZEngine::Windows::FileDialogResult result = {};
        if (!m_scene_open_picker.TryTake(&result))
        {
            return;
        }
        const auto epoch   = m_scene_open_picker_owner.Epoch;
        const bool matches = m_scene_open_picker_owner.Matches(*this);
        m_scene_open_picker_owner.Reset();
        if (!matches || m_closing)
        {
            return;
        }
        if (result.Outcome == ZEngine::Windows::FileDialogStatus::Failed)
        {
            ZENGINE_CORE_ERROR("[Editor] Scene open picker failed")
            return;
        }
        if (m_closing || result.Outcome != ZEngine::Windows::FileDialogStatus::Selected || epoch != Session.SceneEpoch())
        {
            return;
        }
        const auto path = SceneSourcePathFromNative(WorkingSpacePath ? WorkingSpacePath : "", result.Path);
        if (path.Failed() || !RequestSceneOpen(path.Value(), epoch))
            ZENGINE_CORE_WARN("[Editor] Scene open rejected; select a project .zescene source")
    }

    void Editor::OpenScene(const char* path)
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        if (!path || m_closing)
        {
            return;
        }
        size_t length = 0;
        while (length < ZEngine::Core::VFS::VFS_MAX_PATH && path[length])
        {
            ++length;
        }
        const auto source = SceneSourcePathFromNative(WorkingSpacePath ? WorkingSpacePath : "", {path, length});
        if (source.Failed() || !RequestSceneOpen(source.Value(), Session.SceneEpoch()))
            ZENGINE_CORE_WARN("[Editor] Scene open rejected; the current scene was preserved")
    }

    bool Editor::RequestMeshDrop(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        return !m_closing && !m_editor_close_pending && !m_new_scene_pending && !SceneOpenPending() && !SceneChangePromptState().Visible() && owner && owner->Session == &Session && !owner->SourceReadOnly && Session.Editable() && expected_epoch == Session.SceneEpoch() && MeshDrop.Request(path, expected_epoch);
    }

    bool Editor::RequestImportedMesh(const ZEngine::EngineContext& engine, const ZEngine::Core::VFS::VFSPath& path, std::string_view name, const uuids::uuid& scene, uint64_t expected_epoch)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        return !m_closing && !m_editor_close_pending && !m_new_scene_pending && !SceneOpenPending() && !SceneChangePromptState().Visible() && !engine.CloseRequested.value.load(std::memory_order_acquire) && owner && owner->Session == &Session && !scene.is_nil() && owner->UUID == scene && !owner->SourceReadOnly && Session.Editable() && expected_epoch == Session.SceneEpoch() && engine.Scene == Session.Scene() && engine.VFS && MeshDrop.Request(path, expected_epoch, name, engine.VFS, true);
    }

    bool Editor::RequestSceneOpen(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || !owner || owner->Session != &Session || !Session.Ready() || expected_epoch != Session.SceneEpoch() || !IsSceneSourcePath(path) || SceneOpenPending() || m_editor_close_pending || SceneChangePromptState().Visible())
        {
            return false;
        }
        if (!m_scene_open.Owner.Capture(*this))
        {
            return false;
        }
        m_scene_open.Path    = path;
        m_scene_open.Pending = true;
        MeshDrop.Cancel();
        return true;
    }

    bool Editor::ProcessSceneOpen(ZEngine::EngineContext& engine)
    {
        if (!SceneOpenPending())
        {
            return true;
        }
        if (!Session.InMutationPhase())
        {
            return false;
        }
        if (m_scene_open.Pending && !m_scene_open.Owner.Matches(*this))
        {
            m_scene_open.Pending = false;
            return false;
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        // New Scene takes precedence; an unsaved-change prompt must not be
        // overwritten by an older asynchronous Open in the same update.
        if (m_new_scene_pending)
        {
            return true;
        }
        auto*      owner  = static_cast<EditorScene*>(CurrentScene);
        const auto epoch  = m_scene_open_work ? m_scene_open_work->Epoch : m_scene_open.Owner.Epoch;
        const auto finish = [&](bool result) {
            m_scene_change.Finish(SceneChangeKind::OpenScene, epoch);
            return result;
        };
        if (m_closing || !owner || owner->Session != &Session || !engine.Scene || engine.Scene != Session.Scene() || !engine.VFS || !engine.SceneSchemas || epoch != Session.SceneEpoch() || (m_scene_open_work && !m_scene_open_work->Matches(*this, engine)))
        {
            m_scene_open.Pending = false;
            if (m_scene_open_work)
            {
                if (!m_scene_open_work->Cancelled.exchange(true, std::memory_order_acq_rel))
                    ZENGINE_CORE_WARN("[Editor] Scene open no longer belongs to the active scene/project services; the current scene was preserved")
                if (m_scene_open_work->Ready.load(std::memory_order_acquire))
                {
                    m_scene_open_work.reset();
                }
            }
            return finish(false);
        }
        if (m_scene_open_work)
        {
            if (!m_scene_open_work->Ready.load(std::memory_order_acquire))
            {
                return true;
            }
            if (!m_scene_open_work->Succeeded || m_scene_open_work->Cancelled.load(std::memory_order_acquire))
            {
                LogLoadDiagnostics(m_scene_open_work->Diagnostics);
                m_scene_open_work.reset();
                return finish(false);
            }
        }
        // Cancel before asking or replacing; a transient preview is never saved
        // implicitly and never changes what the user's discard decision covers.
        Session.Cancel();
        const auto gate = GateSceneChange(SceneChangeKind::OpenScene);
        if (gate == SceneChangeGate::Waiting)
        {
            return true;
        }
        if (gate != SceneChangeGate::Ready)
        {
            m_scene_open.Pending = false;
            if (m_scene_open_work)
            {
                m_scene_open_work->Cancelled.store(true, std::memory_order_release);
                m_scene_open_work.reset(); // Ready was established above
            }
            return finish(true);
        }
        if (m_scene_open_work)
        {
            auto                                          work      = std::move(m_scene_open_work);
            // Main-thread binding only: no asset-manager/registry pointer escapes
            // its ingest lock, and no worker walks a mutable engine world.
            const EditorSceneAssetResolver                assets    = {.Context = ZEngine::Managers::AssetManager::Instance(), .ResolveMesh = &ResolveCachedMesh};
            ZEngine::Helpers::Scope<EditorSceneCandidate> candidate = {};
            if (!ProjectSourceStillMatches(work->Workspace.c_str(), work->Path))
            {
                work->Diagnostics.Error("Scene source changed or escaped the project; the active scene was preserved.");
                LogLoadDiagnostics(work->Diagnostics);
                return finish(false);
            }
            if (!EditorSceneCandidate::Prepare(engine, std::move(work->Document), *engine.SceneSchemas, work->Path, assets, &candidate, &work->Diagnostics) || !PublishSceneCandidate(engine, candidate, work->Epoch))
            {
                LogLoadDiagnostics(work->Diagnostics);
                return finish(false);
            }
            LogLoadDiagnostics(work->Diagnostics);
            // Recent-history persistence is best-effort chrome, not part of the
            // transaction. It must not turn a successful publication into failure.
            {
                char native[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
                work->Path.ResolveNative(work->Workspace.c_str(), native, sizeof(native));
                auto&      recent      = Components::Recent();
                const auto recent_file = work->Workspace + "/.zengine/recent_levels.txt";
                if (recent.File != recent_file)
                {
                    recent.Load(recent_file);
                }
                recent.Push(native);
            }
            ZENGINE_CORE_INFO("[Editor] Opened scene {}", work->Path.CStr())
            return finish(true);
        }

        m_scene_open.Pending = false;
        if (!ProjectSourceStillMatches(WorkingSpacePath, m_scene_open.Path))
        {
            ZENGINE_CORE_WARN("[Editor] Scene source changed or escaped the project; open cancelled")
            return finish(false);
        }
        auto work         = std::make_unique<SceneOpenWork>();
        work->EngineOwner = &engine;
        work->SceneOwner  = owner;
        work->World       = engine.Scene;
        work->Actors      = engine.ActorManager;
        work->SceneUUID   = owner->UUID;
        work->Assets      = ZEngine::Managers::AssetManager::Instance();
        work->Registry    = work->Assets ? work->Assets->Registry : nullptr;
        work->VFS         = engine.VFS;
        work->Schemas     = engine.SceneSchemas;
        work->Path        = m_scene_open.Path;
        work->Workspace   = WorkingSpacePath;
        work->Epoch       = m_scene_open.Owner.Epoch;
        m_scene_open_work = std::move(work);
        // No inline fallback: a saturated worker queue must not block editor input.
        auto* pool        = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
        if (pool)
        {
            for (uint32_t i = 0; i < pool->WorkerCount; ++i)
            {
                if (pool->SubmitToWorker(i, m_scene_open_work.get(), &SceneOpenWork::Read))
                {
                    return true;
                }
            }
        }
        m_scene_open_work.reset();
        ZENGINE_CORE_WARN("[Editor] Scene open could not obtain a worker; the current scene was preserved")
        return finish(false);
    }

    bool Editor::PublishSceneCandidate(ZEngine::EngineContext& engine, ZEngine::Helpers::Scope<EditorSceneCandidate>& candidate, uint64_t expected_epoch)
    {
        auto* outgoing = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || !Session.InMutationPhase() || !Session.Ready() || !outgoing || outgoing->Session != &Session || !engine.Scene || engine.Scene != Session.Scene() || expected_epoch != Session.SceneEpoch() || !candidate || !candidate->Document().Settings() || candidate->Render().Session)
        {
            return false;
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        Session.Cancel();
        if (outgoing->IsDirty() && !m_scene_change.MayDiscard(SceneChangeKind::OpenScene, expected_epoch, Session.DocumentRevision(), outgoing->ExternalRevision()))
        {
            ZENGINE_CORE_WARN("[Editor] Scene replacement cancelled because the current scene has unsaved edits")
            return false;
        }
        if (!Session.BindScene(nullptr))
        {
            return false;
        }
        DiscardSceneSettings();
        outgoing->Session    = nullptr;
        m_scene_save.Pending = m_new_scene_pending = false;
        m_new_scene_owner.Reset();
        if (engine.WorldCommands)
        {
            engine.WorldCommands->Clear();
        }
        // Restore/cancel before OnDestroy; callbacks still observe the outgoing
        // world. Clear commands again so teardown cannot target recycled IDs later.
        if (engine.ActorManager)
        {
            engine.ActorManager->DestroyAll();
        }
        engine.Scene->ForEachEntity([&](auto entity) { engine.Scene->DestroyEntity(entity); });
        if (engine.WorldCommands)
        {
            engine.WorldCommands->Clear();
        }
        if (!m_loaded_scene)
        {
            m_original_world  = engine.Scene;
            m_original_actors = engine.ActorManager;
        }
        auto retired        = std::move(m_loaded_scene);
        m_loaded_scene      = std::move(candidate);
        engine.Scene        = &m_loaded_scene->World();
        engine.ActorManager = &m_loaded_scene->Actors();
        CurrentScene        = &m_loaded_scene->Render();
        // All preconditions for these allocation-free operations were established
        // before teardown. Retained render payloads own copied data, not this owner.
        const bool bound    = Session.BindScene(engine.Scene) && m_loaded_scene->Render().AttachSession(Session, engine);
        ZENGINE_VALIDATE_ASSERT(bound, "Prepared scene could not bind its editor session")
        m_scene_change.Finish(SceneChangeKind::OpenScene, expected_epoch);
        return bound;
    }
} // namespace Tetragrama

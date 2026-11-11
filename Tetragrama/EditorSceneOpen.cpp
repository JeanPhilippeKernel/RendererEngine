#include <Tetragrama/Components/ZUI/ZUIRecentScenes.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <Tetragrama/Panels/AssetImporterPanel.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <Tetragrama/Thumbnails/ThumbnailService.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Profiling/Profiling.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace Tetragrama
{
    namespace
    {
        struct SceneBindingSet
        {
            ZEngine::ECS::Scene*                     World    = nullptr;
            ZEngine::ECS::ActorManager*              Actors   = nullptr;
            ZEngine::ECS::WorldCommands*             Commands = nullptr;
            ZEngine::ECS::WorldTick*                 Tick     = nullptr;
            ZEngine::Rendering::Scenes::RenderScene* Render   = nullptr;

            static SceneBindingSet                   Capture(const ZEngine::EngineContext& engine, const Editor& editor)
            {
                return {engine.Scene, engine.ActorManager, engine.WorldCommands, engine.WorldTick, editor.CurrentScene};
            }
            void Apply(ZEngine::EngineContext& engine, Editor& editor) const noexcept
            {
                engine.Scene         = World;
                engine.ActorManager  = Actors;
                engine.WorldCommands = Commands;
                engine.WorldTick     = Tick;
                editor.CurrentScene  = Render;
            }
        };

        /// Compatibility adapter for existing OnDestroy callbacks that borrow
        /// EngineContext instead of their actor's own scene. Only on the main
        /// mutation thread, AFTER commit, while publication and session mutation
        /// are sealed. Render workers consume immutable snapshots, not these views.
        /// It never makes the retired world active in the publication authority.
        struct LegacyRetirementBindings
        {
            ZEngine::EngineContext& Engine;
            Editor&                 EditorOwner;
            SceneBindingSet         Committed = {};

            LegacyRetirementBindings(ZEngine::EngineContext& engine, Editor& editor, const SceneBindingSet& retired) : Engine(engine), EditorOwner(editor), Committed(SceneBindingSet::Capture(engine, editor))
            {
                retired.Apply(Engine, EditorOwner);
            }
            ~LegacyRetirementBindings()
            {
                Committed.Apply(Engine, EditorOwner);
            }
        };

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
        ZEngine::EngineContext*                           EngineOwner            = nullptr;
        EditorScene*                                      SceneOwner             = nullptr;
        ZEngine::ECS::Scene*                              World                  = nullptr;
        ZEngine::ECS::ActorManager*                       Actors                 = nullptr;
        ZEngine::ECS::WorldCommands*                      Commands               = nullptr;
        ZEngine::ECS::WorldTick*                          Tick                   = nullptr;
        uuids::uuid                                       SceneUUID              = {};
        ZEngine::Core::VFS::IVFSContext*                  VFS                    = nullptr;
        const ZEngine::ECS::SceneComponentSchemaRegistry* Schemas                = nullptr;
        const ZEngine::ECS::SceneMigrationRegistry*       Migrations             = nullptr;
        ZEngine::Core::VFS::VFSPath                       Path                   = {};
        std::string                                       Workspace              = {};
        uint64_t                                          Epoch                  = 0;
        uint64_t                                          Generation             = 0;
        ZEngine::ECS::SceneRequestId                      Request                = {};
        bool                                              RetainResult           = false;
        ZEngine::ECS::SceneLoadStage                      FailureStage           = ZEngine::ECS::SceneLoadStage::Read;
        ZEngine::ECS::SceneStorageLease                   Storage                = {};
        std::unique_ptr<ZEngine::ECS::SceneInstance>      Instance               = {};
        ZEngine::Helpers::Scope<EditorSceneCandidate>     Candidate              = {};
        size_t                                            DocumentWorkingBytes   = 0;
        ZEngine::ECS::SceneDiagnostics                    Diagnostics            = {};
        PaddedAtomic<bool>                                Ready                  = {};
        PaddedAtomic<bool>                                Cancelled              = {};
        bool                                              Succeeded              = false;
        bool                                              SourceValidatedOnOwner = false;
        ZEngine::Managers::AssetManager*                  Assets                 = nullptr;
        ZEngine::Core::VFS::AssetRegistry*                Registry               = nullptr;

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
            if (!engine.ScenePublication.Idle() || Generation != engine.ScenePublication.Generation() || DocumentWorkingBytes != engine.SceneDocumentWorkingBytes || EngineOwner != &engine || SceneOwner != editor.CurrentScene || World != engine.Scene || Actors != engine.ActorManager || Commands != engine.WorldCommands || Tick != engine.WorldTick || VFS != engine.VFS || Schemas != engine.SceneSchemas || Migrations != engine.SceneMigrations || !SceneOwner || SceneOwner->UUID != SceneUUID || !editor.WorkingSpacePath)
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
            ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.Request");
            auto& work = *static_cast<SceneOpenWork*>(context);
            {
                if (!work.Cancelled.value.load(std::memory_order_acquire))
                {
                    if (!ProjectSourceStillMatches(work.Workspace.c_str(), work.Path))
                    {
                        work.Diagnostics.Error("Scene source changed or escaped the project; the active scene was preserved.");
                    }
                    else
                    {
                        // Initialize only after dispatch. A saturated queue must
                        // not repeatedly construct ECS arrays on the UI thread.
                        work.FailureStage = ZEngine::ECS::SceneLoadStage::Preparation;
                        work.Succeeded    = ZEngine::ECS::SceneInstance::Create(std::move(work.Storage), &work.Instance, &work.Diagnostics);
                        if (work.Succeeded && !work.Cancelled.value.load(std::memory_order_acquire))
                        {
                            work.FailureStage = ZEngine::ECS::SceneLoadStage::Read;
                            work.Succeeded    = work.Instance->ReadSource(*work.VFS, work.Path, *work.Schemas, work.DocumentWorkingBytes, &work.Diagnostics, {}, work.Migrations);
                        }
                        if (work.Succeeded && !work.Cancelled.value.load(std::memory_order_acquire))
                        {
                            work.FailureStage = ZEngine::ECS::SceneLoadStage::Assets;
                            work.Succeeded    = work.LoadMeshes();
                        }
                        if (work.Succeeded && !work.Cancelled.value.load(std::memory_order_acquire))
                        {
                            work.FailureStage = ZEngine::ECS::SceneLoadStage::Preparation;
                            work.Succeeded    = work.Instance->PopulateSource(*work.Schemas, &work.Diagnostics);
                            if (work.Succeeded && !work.Cancelled.value.load(std::memory_order_acquire))
                            {
                                // Only the unpublished owner is touched. The
                                // resolver takes the cache ingest lock and returns
                                // a value, never a borrowed mutable cache pointer.
                                const EditorSceneAssetResolver assets = {.Context = work.Assets, .ResolveMesh = &ResolveCachedMesh};
                                work.Succeeded                        = EditorSceneCandidate::PreparePopulated(std::move(work.Instance), work.Path, assets, &work.Candidate, &work.Diagnostics);
                            }
                        }
                    }
                }
            }
            work.Ready.value.store(true, std::memory_order_release);
        }

        bool LoadMeshes()
        {
            const auto& Document = Instance->Document();
            // Immutable semantic references, not a temporary ECS world or source
            // YAML inspection. Reserve once, sort/deduplicate before any asset IO.
            auto*       scratch  = Instance ? Instance->Arena() : nullptr;
            if (!scratch)
            {
                Diagnostics.Error("Scene mesh references require admitted operation-local storage.");
                return false;
            }
            // This allocation precedes the temporary reference-list checkpoint:
            // its pins must survive worker completion, publication and retirement.
            if (!Instance->SourceAssets().Initialize(scratch))
            {
                Diagnostics.Error("Cannot retain CPU asset ownership within scene storage.");
                return false;
            }
            struct ScratchScope
            {
                ZEngine::Core::Memory::ArenaTemp Mark     = {};
                ZEngine::ECS::SceneInstance*     Instance = nullptr;
                ~ScratchScope()
                {
                    Instance->RecordMemoryUsage();
                    ZEngine::Core::Memory::EndTempArena(Mark);
                }
            } scope{ZEngine::Core::Memory::BeginTempArena(scratch), Instance.get()};
            const size_t                                  capacity = Document.EntityCount();
            ZEngine::Core::Containers::Array<uuids::uuid> meshes   = {};
            meshes.m_allocator                                     = scratch;
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
                if (Cancelled.value.load(std::memory_order_acquire))
                {
                    return false;
                }
                const auto loaded = Assets->LoadMeshAsset(*VFS, uuid, {}, &Cancelled.value, &Instance->SourceAssets());
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

    void Editor::ReleaseSceneOpenWaiting()
    {
        if (m_scene_open_request && m_scene_load_engine && !m_scene_open_retain_result)
        {
            m_scene_load_engine->SceneLoads.Acknowledge(m_scene_open_request);
        }
        m_scene_open.Pending       = false;
        m_scene_open_request       = {};
        m_scene_open_retain_result = false;
    }

    void Editor::ReleaseSceneOpenWork()
    {
        if (!m_scene_open_work)
        {
            return;
        }
        auto&      loads   = m_scene_open_work->EngineOwner->SceneLoads;
        const auto request = m_scene_open_work->Request;
        const bool retain  = m_scene_open_work->RetainResult;
        m_scene_open_work.reset();
        const bool cleaned = loads.FinishCleanup(request);
        ZENGINE_VALIDATE_ASSERT(cleaned, "Scene work cleanup lost its admitted request")
        if (!retain)
        {
            loads.Acknowledge(request);
        }
    }

    void Editor::CancelSceneOpen()
    {
        m_scene_change.Reset();
        m_closing = true;
        if (m_scene_load_engine)
        {
            m_scene_load_engine->SceneLoads.Shutdown();
        }
        ReleaseSceneOpenWaiting();
        m_scene_open_picker_owner.Reset();
        if (m_scene_open_work)
        {
            m_scene_open_work->Cancelled.value.store(true, std::memory_order_release);
        }
        // Retain the context even if the pool never started this queued task.
        // OnWorldShutdown releases it only after every worker has joined.
    }

    void Editor::OnWorldShutdown(ZEngine::EngineContext& engine)
    {
        if (m_publishing_scene || m_processing_scene_open)
        {
            return;
        }
        if (ContentBrowser)
        {
            ContentBrowser->ShutdownListingsAfterWorkersJoined();
            ContentBrowser = nullptr;
        }
        if (AssetThumbnails)
        {
            // Render and import workers have joined; registry and RRM are still
            // live here. Arena-owned services have no automatic destructor.
            AssetThumbnails->Deinitialize();
            AssetThumbnails = nullptr;
        }
        CancelSceneOpen();
        MeshDrop.ShutdownAfterWorkersJoined();
        if (AssetImporter)
        {
            AssetImporter->ShutdownAfterWorkersJoined();
        }
        ReleaseSceneOpenWork(); // All workers have joined, including unstarted jobs.
        m_scene_load_engine = nullptr;
        Session.Shutdown();
        if (CurrentScene)
        {
            static_cast<EditorScene*>(CurrentScene)->Session = nullptr;
            static_cast<EditorScene*>(CurrentScene)->ReleaseSourceAssetPins();
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
            engine.Scene         = m_original_world;
            engine.ActorManager  = m_original_actors;
            engine.WorldCommands = m_original_commands;
            engine.WorldTick     = m_original_tick;
            CurrentScene         = nullptr;
        }
        m_original_world    = nullptr;
        m_original_actors   = nullptr;
        m_original_commands = nullptr;
        m_original_tick     = nullptr;
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

    bool Editor::RequestSceneOpen(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch, ZEngine::ECS::SceneRequestId* request)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || m_publishing_scene || m_processing_scene_open || !owner || owner->Session != &Session || !Session.Ready() || expected_epoch != Session.SceneEpoch() || !IsSceneSourcePath(path) || m_editor_close_pending || SceneChangePromptState().Visible())
        {
            return false;
        }
        SceneFileOwner snapshot = {};
        auto*          engine   = owner->SessionEngine;
        if (!engine || !engine->ScenePublication.Idle() || engine->CloseRequested.value.load(std::memory_order_acquire) || (SceneOpenPending() && m_scene_load_engine != engine) || !snapshot.Capture(*this))
        {
            return false;
        }
        ZEngine::ECS::SceneRequestId admitted = {};
        if (!engine->SceneLoads.Admit(engine->ScenePublication.Generation(), &admitted))
        {
            return false;
        }
        // Admission succeeded before invalidating any older input or worker.
        ReleaseSceneOpenWaiting();
        if (m_scene_open_work)
        {
            m_scene_open_work->Cancelled.value.store(true, std::memory_order_release);
        }
        m_scene_change.Finish(SceneChangeKind::OpenScene, expected_epoch);
        m_scene_load_engine        = engine;
        m_scene_open_request       = admitted;
        m_scene_open_retain_result = request != nullptr;
        m_scene_open.Owner         = snapshot;
        m_scene_open.Path          = path;
        m_scene_open.Pending       = true;
        if (request)
        {
            *request = admitted;
        }
        MeshDrop.Cancel();
        return true;
    }

    bool Editor::ProcessSceneOpen(ZEngine::EngineContext& engine)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.Process");
        if (!Session.OnOwnerThread() || m_processing_scene_open || m_publishing_scene || !engine.ScenePublication.Idle())
        {
            return false;
        }
        if (!SceneOpenPending())
        {
            return true;
        }
        if (!Session.InMutationPhase())
        {
            return false;
        }
        struct ProcessingScope
        {
            bool& Active;
            explicit ProcessingScope(bool& active) : Active(active)
            {
                Active = true;
            }
            ~ProcessingScope()
            {
                Active = false;
            }
        } processing(m_processing_scene_open);
        // Terminal outcome can precede worker completion. Never free or replace
        // this payload until Ready, even if newer input is already waiting.
        if (m_scene_open_work)
        {
            if (!m_scene_open_work->EngineOwner->SceneLoads.Pending(m_scene_open_work->Request))
            {
                m_scene_open_work->Cancelled.value.store(true, std::memory_order_release);
                if (!m_scene_open_work->Ready.value.load(std::memory_order_acquire))
                {
                    return true;
                }
                m_scene_change.Finish(SceneChangeKind::OpenScene, m_scene_open_work->Epoch);
                ReleaseSceneOpenWork();
                if (!m_scene_open.Pending)
                {
                    return true;
                }
            }
        }
        if (m_scene_open.Pending)
        {
            if (!m_scene_load_engine->SceneLoads.Pending(m_scene_open_request))
            {
                m_scene_change.Finish(SceneChangeKind::OpenScene, m_scene_open.Owner.Epoch);
                ReleaseSceneOpenWaiting();
                return true;
            }
        }
        if (m_scene_open.Pending && !m_scene_open.Owner.Matches(*this))
        {
            m_scene_load_engine->SceneLoads.Fail(m_scene_open_request, ZEngine::ECS::SceneLoadStage::Admission, ZEngine::ECS::SceneLoadError::Invalidated);
            ReleaseSceneOpenWaiting();
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
        const auto finish = [&](bool result, ZEngine::ECS::SceneLoadStage stage = ZEngine::ECS::SceneLoadStage::Publication, ZEngine::ECS::SceneLoadError error = ZEngine::ECS::SceneLoadError::Invalidated) {
            m_scene_change.Finish(SceneChangeKind::OpenScene, epoch);
            if (m_scene_open_work)
            {
                m_scene_open_work->EngineOwner->SceneLoads.Fail(m_scene_open_work->Request, stage, error);
                if (m_scene_open_work->Ready.value.load(std::memory_order_acquire))
                {
                    ReleaseSceneOpenWork();
                }
            }
            if (m_scene_open_request)
            {
                m_scene_load_engine->SceneLoads.Fail(m_scene_open_request, stage, error);
                ReleaseSceneOpenWaiting();
            }
            return result;
        };
        ZEngine::ECS::SceneLoadResult waiting       = {};
        const bool                    stale_waiting = m_scene_open.Pending && (m_scene_load_engine != &engine || !engine.SceneLoads.Poll(m_scene_open_request, &waiting) || waiting.ExpectedGeneration != engine.ScenePublication.Generation());
        if (m_closing || stale_waiting || !owner || owner->Session != &Session || !engine.Scene || engine.Scene != Session.Scene() || !engine.VFS || !engine.SceneSchemas || epoch != Session.SceneEpoch() || (m_scene_open_work && !m_scene_open_work->Matches(*this, engine)))
        {
            if (m_scene_open_work)
            {
                if (!m_scene_open_work->Cancelled.value.exchange(true, std::memory_order_acq_rel))
                {
                    ZENGINE_CORE_WARN("[Editor] Scene open no longer belongs to the active scene/project services; the current scene was preserved")
                }
            }
            return finish(false);
        }
        if (m_scene_open_work)
        {
            if (!m_scene_open_work->Ready.value.load(std::memory_order_acquire))
            {
                return true;
            }
            if (!m_scene_open_work->Succeeded || m_scene_open_work->Cancelled.value.load(std::memory_order_acquire))
            {
                LogLoadDiagnostics(m_scene_open_work->Diagnostics);
                const bool resource_failure = m_scene_open_work->FailureStage == ZEngine::ECS::SceneLoadStage::Preparation || m_scene_open_work->Diagnostics.HasError(ZEngine::ECS::SceneDiagnosticCode::AllocationFailed) || m_scene_open_work->Diagnostics.HasError(ZEngine::ECS::SceneDiagnosticCode::LimitExceeded);
                return finish(false, m_scene_open_work->FailureStage, resource_failure ? ZEngine::ECS::SceneLoadError::Resources : ZEngine::ECS::SceneLoadError::Source);
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
            if (m_scene_open_request)
            {
                m_scene_load_engine->SceneLoads.Cancel(m_scene_open_request);
            }
            if (m_scene_open_work)
            {
                engine.SceneLoads.Cancel(m_scene_open_work->Request);
                m_scene_open_work->Cancelled.value.store(true, std::memory_order_release);
            }
            return finish(true);
        }
        if (m_scene_open_work)
        {
            auto* work = m_scene_open_work.get();
            if (!work->SourceValidatedOnOwner && !ProjectSourceStillMatches(work->Workspace.c_str(), work->Path))
            {
                work->Diagnostics.Error("Scene source changed or escaped the project; the active scene was preserved.");
                LogLoadDiagnostics(work->Diagnostics);
                return finish(false, ZEngine::ECS::SceneLoadStage::Read, ZEngine::ECS::SceneLoadError::Source);
            }
            work->SourceValidatedOnOwner = true;
            if (RenderPipeline && engine.RenderResourceManager)
            {
                if (work->Candidate->RequiredMeshCount() > ZEngine::Rendering::RenderResourceManager::SceneGeometryMeshCapacity)
                {
                    work->Diagnostics.Error("The complete scene exceeds the retained mesh resource capacity.");
                    LogLoadDiagnostics(work->Diagnostics);
                    return finish(false, ZEngine::ECS::SceneLoadStage::Preparation, ZEngine::ECS::SceneLoadError::Resources);
                }
                ZEngine::Rendering::Scenes::SceneInitialView view = {};
                if (RenderPipeline->Device)
                {
                    ZEngine::Applications::RenderFrameState captured = {};
                    if (!CaptureRenderView(captured) || !captured.RenderTargetW || !captured.RenderTargetH)
                    {
                        return true; // No drawable viewport yet; keep the outgoing scene.
                    }
                    view               = {captured.Camera, captured.RenderTargetW, captured.RenderTargetH, captured.ResizeSequence, Session.GizmoSettings().ShowGrid, true};
                    view.GridLabelFont = CaptureGridLabelFont();
                }
                if (!work->Candidate->PrepareRenderResources(*engine.RenderResourceManager, work->Request, work->Generation, view))
                {
                    return true; // Both fixed request slots may still have mailbox/GPU owners.
                }
                ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult resources = {};
                if (!work->Candidate->PollRenderResources(&resources) || resources.State == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationState::Failed || resources.State == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationState::Releasing)
                {
                    if (resources.Error == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationError::RequiredMaterialBinding)
                    {
                        work->Diagnostics.Error("An authored material texture is missing, failed, mismatched, or a debug fallback; the active scene was preserved.");
                    }
                    else if (resources.Error == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationError::RequiredEnvironment)
                    {
                        work->Diagnostics.Error("The required sky environment could not be prepared; check its source, selected sun, device support and environment memory budget. The active scene was preserved.");
                    }
                    else if (resources.Error == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationError::RequiredPipeline)
                    {
                        work->Diagnostics.Error("A required scene shader/pipeline variant could not be prepared; check host shaders, device support and pipeline-worker availability. The active scene was preserved.");
                    }
                    else if (resources.Error == ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationError::RequiredDescriptor)
                    {
                        work->Diagnostics.Error("Required material texture descriptors could not be prepared for their retained images; the active scene was preserved.");
                    }
                    else
                    {
                        work->Diagnostics.Error("Required render resources could not be prepared; the active scene was preserved.");
                    }
                    LogLoadDiagnostics(work->Diagnostics);
                    return finish(false, ZEngine::ECS::SceneLoadStage::Preparation, ZEngine::ECS::SceneLoadError::Resources);
                }
                if (resources.State != ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationState::ResourcesRetained)
                {
                    return true;
                }
            }
            if (!PublishSceneCandidate(engine, work->Candidate, work->Epoch, work->Generation, work->Request))
            {
                LogLoadDiagnostics(work->Diagnostics);
                work->Candidate.reset();
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

        auto* pool = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
        if (!pool || !pool->WorkerCount)
        {
            return true;
        }
        ZEngine::ECS::SceneStorageLease storage = {};
        if (!engine.SceneDocumentWorkingBytes || engine.SceneDocumentWorkingBytes > std::numeric_limits<size_t>::max() - ZEngine::ECS::SceneStorageLease::MinimumBytes)
        {
            return finish(false, ZEngine::ECS::SceneLoadStage::Preparation, ZEngine::ECS::SceneLoadError::Resources);
        }
        const auto admission = engine.AdmitSceneStorage(&storage, engine.SceneDocumentWorkingBytes + ZEngine::ECS::SceneStorageLease::MinimumBytes);
        if (admission == ZEngine::ECS::SceneStorageAdmission::Busy)
        {
            return true;
        }
        if (admission != ZEngine::ECS::SceneStorageAdmission::Acquired)
        {
            return finish(false, ZEngine::ECS::SceneLoadStage::Preparation, ZEngine::ECS::SceneLoadError::Resources);
        }
        ZEngine::ECS::SceneDiagnostics admission_diagnostics = {};
        if (!engine.SceneSchemas->FreezeForWorker(&admission_diagnostics) || (engine.SceneMigrations && !engine.SceneMigrations->Freeze()))
        {
            LogLoadDiagnostics(admission_diagnostics);
            return finish(false, ZEngine::ECS::SceneLoadStage::Preparation, ZEngine::ECS::SceneLoadError::UnsupportedCapability);
        }
        if (!ProjectSourceStillMatches(WorkingSpacePath, m_scene_open.Path))
        {
            ZENGINE_CORE_WARN("[Editor] Scene source changed or escaped the project; open cancelled")
            return finish(false, ZEngine::ECS::SceneLoadStage::Read, ZEngine::ECS::SceneLoadError::Source);
        }
        auto work                  = std::make_unique<SceneOpenWork>();
        work->Storage              = std::move(storage);
        work->DocumentWorkingBytes = engine.SceneDocumentWorkingBytes;
        work->EngineOwner          = &engine;
        work->SceneOwner           = owner;
        work->World                = engine.Scene;
        work->Actors               = engine.ActorManager;
        work->Commands             = engine.WorldCommands;
        work->Tick                 = engine.WorldTick;
        work->SceneUUID            = owner->UUID;
        work->Assets               = ZEngine::Managers::AssetManager::Instance();
        work->Registry             = work->Assets ? work->Assets->Registry : nullptr;
        work->VFS                  = engine.VFS;
        work->Schemas              = engine.SceneSchemas;
        work->Migrations           = engine.SceneMigrations;
        work->Path                 = m_scene_open.Path;
        work->Workspace            = WorkingSpacePath;
        work->Epoch                = m_scene_open.Owner.Epoch;
        work->Generation           = waiting.ExpectedGeneration;
        work->Request              = m_scene_open_request;
        work->RetainResult         = m_scene_open_retain_result;
        if (!engine.SceneLoads.TryStart(work->Request))
        {
            return true;
        }
        m_scene_open_work          = std::move(work);
        m_scene_open.Pending       = false;
        m_scene_open_request       = {};
        m_scene_open_retain_result = false;
        // No inline fallback: a saturated worker queue must not block editor input.
        for (uint32_t i = 0; i < pool->WorkerCount; ++i)
        {
            if (pool->SubmitToWorker(i, m_scene_open_work.get(), &SceneOpenWork::Read))
            {
                return true;
            }
        }
        // Backpressure, not a failed scene: retry without source I/O or retaining
        // candidate storage. No worker received this payload on any queue.
        const auto request         = m_scene_open_work->Request;
        m_scene_open_request       = request;
        m_scene_open_retain_result = m_scene_open_work->RetainResult;
        m_scene_open_work.reset();
        engine.SceneLoads.DeferUnsubmitted(request);
        m_scene_open.Pending = true;
        return true;
    }

    bool Editor::PublishSceneCandidate(ZEngine::EngineContext& engine, ZEngine::Helpers::Scope<EditorSceneCandidate>& candidate, uint64_t expected_epoch, uint64_t expected_generation, ZEngine::ECS::SceneRequestId request)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.PublicationAndRetirement");
        // Reject reentry before inspecting any callback-scoped legacy binding.
        if (!Session.OnOwnerThread() || !engine.ScenePublication.Idle() || m_publishing_scene || engine.CloseRequested.value.load(std::memory_order_acquire))
        {
            return false;
        }
        if (!expected_generation)
        {
            expected_generation = engine.ScenePublication.Generation();
        }
        auto* outgoing = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || expected_generation != engine.ScenePublication.Generation() || !Session.InMutationPhase() || !Session.Ready() || !outgoing || outgoing->Session != &Session || !engine.Scene || engine.Scene != Session.Scene() || expected_epoch != Session.SceneEpoch() || !candidate || !candidate->Document().Settings() || candidate->Render().Session || candidate->Actors().BoundScene() != &candidate->World() || !candidate->Commands().IsEmpty() || candidate->Tick().SystemCount())
        {
            return false;
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        if (request && RenderPipeline && engine.RenderResourceManager)
        {
            ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult resources = {};
            if (!candidate->PollRenderResources(&resources) || resources.Id.Request != request || resources.ExpectedGeneration != expected_generation || resources.State != ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationState::ResourcesRetained)
            {
                return false;
            }
        }
        Session.Cancel();
        if (outgoing->IsDirty() && !m_scene_change.MayDiscard(SceneChangeKind::OpenScene, expected_epoch, Session.DocumentRevision(), outgoing->ExternalRevision()))
        {
            ZENGINE_CORE_WARN("[Editor] Scene replacement cancelled because the current scene has unsaved edits")
            return false;
        }
        EditorSession::PreparedSceneBinding binding = {};
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.SessionPreflight");
            if (!Session.PrepareSceneReplacement(candidate->World(), candidate->Render().MutationHooks(), candidate->Render().SettingsHooks(), &binding) || !Session.CanCommitSceneReplacement(binding))
            {
                return false;
            }
        }

        ZEngine::Applications::AppRenderPipeline::PreparedFrameState initial_frame = {};
        if (RenderPipeline)
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.InitialSnapshot");
            // Candidate-only work before the publication guard. Never borrow
            // outgoing entity selection, gizmos, or asynchronous pick callbacks.
            ZEngine::Applications::RenderFrameState frame = {};
            if (!CaptureRenderView(frame))
            {
                return false;
            }
            // Preflight already synchronized hierarchy. An asynchronous request's
            // immutable resource input already captured exact transforms/lights;
            // do not repeat the authoring ID join on the publication frame.
            if (!request)
            {
                ZEngine::ECS::Systems::SyncECSToRenderScene(candidate->World(), 1.f, candidate->Render());
                ZEngine::ECS::Systems::SyncECSToLights(candidate->World(), candidate->Render());
            }
            frame.SceneGeneration = expected_generation + 1;
            frame.SceneResources  = candidate->InitialRenderResources();
            frame.ShowGrid        = Session.GizmoSettings().ShowGrid;
            frame.RenderOverlay   = EnableRenderOverlay;
            frame.FramebufferW    = CurrentWindow ? CurrentWindow->GetWidth() : 0;
            frame.FramebufferH    = CurrentWindow ? CurrentWindow->GetHeight() : 0;
            if (!RenderPipeline->PrepareFrameState(frame, &candidate->Render(), &initial_frame))
            {
                return false;
            }
        }

        const auto                                    old_bindings = SceneBindingSet::Capture(engine, *this);
        ZEngine::Helpers::Scope<EditorSceneCandidate> retired      = {};
        ZEngine::ECS::ScenePublication::Transition    transition   = {};
        if (!engine.ScenePublication.TryBegin(expected_generation, &transition))
        {
            return false;
        }
        if (request && !engine.SceneLoads.BeginCommit(request, expected_generation))
        {
            return false;
        }
        // Guarded commit: only prepared pointer/owner/metadata switches. In
        // particular, history reset does not call Destroy and attach cannot fail.
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.GuardedCommit");
            m_publishing_scene = true;
            DiscardSceneSettings();
            outgoing->Session    = nullptr;
            m_scene_save.Pending = m_new_scene_pending = false;
            m_new_scene_owner.Reset();
            if (!m_loaded_scene)
            {
                m_original_world    = engine.Scene;
                m_original_actors   = engine.ActorManager;
                m_original_commands = engine.WorldCommands;
                m_original_tick     = engine.WorldTick;
            }
            retired                             = std::move(m_loaded_scene);
            m_loaded_scene                      = std::move(candidate);
            const SceneBindingSet next_bindings = {&m_loaded_scene->World(), &m_loaded_scene->Actors(), &m_loaded_scene->Commands(), &m_loaded_scene->Tick(), &m_loaded_scene->Render()};
            next_bindings.Apply(engine, *this);
            Session.CommitSceneReplacement(binding);
            m_loaded_scene->Render().CommitSessionAttachment(Session, engine);
            transition.Publish();
            if (initial_frame)
            {
                initial_frame.Publish();
            }
            if (request)
            {
                const bool committed = engine.SceneLoads.Commit(request, engine.ScenePublication.Generation());
                ZENGINE_VALIDATE_ASSERT(committed, "Admitted scene commit lost its reserved result")
            }
        }
        // Outside commit: the old owner and all its views are still retained.
        // Callback compatibility is explicit and cannot admit edits, loads,
        // shutdown, or simulation through this temporary outgoing context.
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.LegacyRetirement");
            LegacyRetirementBindings legacy(engine, *this, old_bindings);
            Session.RetireSceneHistory();
            if (old_bindings.Commands)
            {
                old_bindings.Commands->Clear();
            }
            if (old_bindings.Actors)
            {
                old_bindings.Actors->DestroyAll();
            }
            old_bindings.World->ForEachEntity([&](auto entity) { old_bindings.World->DestroyEntity(entity); });
            if (old_bindings.Commands)
            {
                old_bindings.Commands->Clear();
            }
            outgoing->ReleaseSourceAssetPins();
            retired.reset();
        }
        Session.FinishSceneReplacement();
        m_scene_change.Finish(SceneChangeKind::OpenScene, expected_epoch);
        m_publishing_scene = false;
        return true;
    }
} // namespace Tetragrama

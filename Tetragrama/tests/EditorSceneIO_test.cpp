#include <Tetragrama/Components/ZUI/ZUIRecentScenes.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>

using namespace Tetragrama;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace
{
    struct SaveFaultVFS : VFSContext
    {
        bool            FailRename = false;
        VFSResult<void> Rename(const VFSPath& from, const VFSPath& to) override
        {
            return FailRename ? VFSResult<void>::Fail(VFSError::IOError) : VFSContext::Rename(from, to);
        }
    };
    struct SceneOpenReadGate : VFSContext
    {
        std::atomic<bool>     Started   = false;
        std::atomic<bool>     Release   = false;
        std::atomic<uint32_t> MeshReads = 0;
        bool                  OnlyMesh  = false;

        VFSResult<IVFSFile*>  Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            const bool mesh = path.Extension().Equals(".zemesh");
            if (mesh)
            {
                MeshReads.fetch_add(1, std::memory_order_relaxed);
            }
            if (OnlyMesh && !mesh)
            {
                return VFSContext::Open(path, flags);
            }
            Started.store(true, std::memory_order_release);
            while (!Release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            return VFSContext::Open(path, flags);
        }
    };
    enum class SceneOpenOwnerChange : uint8_t
    {
        VFS,
        Schemas,
        Workspace,
        EmptyWorkspace,
        NullWorkspace,
        RenderOwner,
        SceneUUID,
        World,
        Actors,
        Engine,
    };
    enum class ScenePickerOwnerChange : uint8_t
    {
        RenderOwner,
        SceneUUID,
        Window,
        Configuration,
        ConfiguredWorkspace,
        Workspace,
        NullWorkspace,
        EmptyWorkspace,
        ShorterWorkspace,
    };
    struct SceneSavePicker : ZEngine::Windows::CoreWindow
    {
        const EditorSession*                ObservedSession = nullptr;
        bool                                ActiveAtOpen    = false;
        ZEngine::Windows::FileDialogRequest Result          = ZEngine::Windows::FileDialogRequest::Create();
        ZEngine::Windows::FileDialogRequest OpenResult      = {};
        uint32_t                            Requests        = 0;
        uint32_t                            OpenRequests    = 0;
        std::string                         SuggestedName   = {};
        std::string                         SaveDirectory   = {};
        std::string                         OpenDirectory   = {};
        ZEngine::Windows::WindowProperty    Property        = {};

        uint32_t                            GetHeight() const override
        {
            return 100;
        }
        uint32_t GetWidth() const override
        {
            return 100;
        }
        ZEngine::Core::Containers::StringView GetTitle() const override
        {
            return {};
        }
        void SetTitle(ZEngine::Core::Containers::StringView) override {}
        bool IsMinimized() const override
        {
            return false;
        }
        bool IsVSyncEnable() const override
        {
            return false;
        }
        void                                    SetVSync(bool) override {}
        void                                    SetCallbackFunction(const EventCallbackFn&) override {}
        const ZEngine::Windows::WindowProperty& GetWindowProperty() const override
        {
            return Property;
        }
        bool CreateSurface(void*, void**) override
        {
            return false;
        }
        void* GetNativeWindow() const override
        {
            return nullptr;
        }
        void  PollEvent() override {}
        float GetTime() override
        {
            return 0.f;
        }
        float GetDeltaTime() override
        {
            return 0.f;
        }
        bool OnEvent(ZEngine::Core::CoreEvent&) override
        {
            return false;
        }
        bool OnWindowClosed(ZEngine::Windows::Events::WindowClosedEvent&) override
        {
            return false;
        }
        bool OnWindowResized(ZEngine::Windows::Events::WindowResizedEvent&) override
        {
            return false;
        }
        bool OnWindowMinimized(ZEngine::Windows::Events::WindowMinimizedEvent&) override
        {
            return false;
        }
        bool OnWindowMaximized(ZEngine::Windows::Events::WindowMaximizedEvent&) override
        {
            return false;
        }
        bool OnWindowRestored(ZEngine::Windows::Events::WindowRestoredEvent&) override
        {
            return false;
        }
        ZEngine::Windows::FileDialogRequest OpenFileDialogAsync(std::span<std::string_view>, std::string_view directory, std::string_view) override
        {
            ActiveAtOpen = ObservedSession && ObservedSession->Active();
            ++OpenRequests;
            OpenDirectory = directory;
            OpenResult    = ZEngine::Windows::FileDialogRequest::Create();
            return OpenResult;
        }
        ZEngine::Windows::FileDialogRequest SaveFileDialogAsync(std::span<std::string_view> filters, std::string_view directory, std::string_view filename, std::string_view) override
        {
            EXPECT_EQ(filters.size(), 1u);
            if (!filters.empty())
            {
                EXPECT_EQ(filters.front(), ".zescene");
            }
            ++Requests;
            SaveDirectory = directory;
            SuggestedName = filename;
            Result        = ZEngine::Windows::FileDialogRequest::Create();
            return Result;
        }
    };
} // namespace

class EditorSceneIOTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    Memory          = {};
    ZEngine::Core::Memory::MemoryManager    AssetMemory     = {};
    bool                                    OwnAssets       = false;
    Scene                                   ECS             = {};
    SceneComponentSchemaRegistry            Schemas         = {};
    SceneDiagnostics                        Diagnostics     = {};
    VFSMemoryBackend                        Backend         = {};
    SaveFaultVFS                            VFS             = {};
    SceneOpenReadGate                       ReadGate        = {};
    std::atomic<uint32_t>                   CompletedFences = 0;
    EditorScene                             Owner           = {};
    std::unique_ptr<Editor>                 App             = {};
    std::unique_ptr<ZEngine::EngineContext> Engine          = {};
    EditorConfiguration                     Configuration   = {};
    SceneSavePicker                         Picker          = {};
    Tetragrama::Components::RecentScenes    PreviousRecent  = {};
    std::filesystem::path                   Temporary       = {};
    std::string                             Workspace       = {};
    EntityID                                Entity          = {};
    VFSPath                                 Path            = VFSPath::Parse("/Scenes/main.zescene").Value();

    void                                    SetUp() override
    {
        PreviousRecent = std::exchange(Tetragrama::Components::Recent(), {});
        Memory.Initialize(ZMega(128), {});
        ECS.Initialize(&Memory.MainArena);
        Schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
        Backend.Initialize(&Memory.MainArena);
        VFS.Initialize(&Memory.MainArena);
        ASSERT_TRUE(VFS.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        App = std::make_unique<Editor>();
        ASSERT_TRUE(App->Session.Initialize(&Memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        Entity = ECS.CreateEntity();
        ECS.AddComponent<UUIDComponent>(Entity, {*uuids::uuid::from_string("22222222-2222-4222-8222-222222222222")});
        ECS.AddComponent<TransformComponent>(Entity, {});
        Owner.UUID        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        Owner.Name        = "Main Scene";
        Owner.Session     = &App->Session;
        App->CurrentScene = &Owner;
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(App->Session.BindScene(&ECS));
        App->Session.EndMutationPhase();

        static std::atomic_uint64_t next = 0;
        Temporary                        = std::filesystem::temp_directory_path() / ("zengine-editor-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(Temporary / "project" / "Scenes");
        std::filesystem::create_directories(Temporary / "project-other");
        Workspace             = (Temporary / "project").string();
        App->WorkingSpacePath = Workspace.c_str();
        Configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
        Configuration.ScenePath.init(&Memory.MainArena, "Scenes");
        App->Configuration   = &Configuration;
        App->CurrentWindow   = &Picker;
        Engine               = std::make_unique<ZEngine::EngineContext>();
        Engine->Scene        = &ECS;
        Engine->VFS          = &VFS;
        Engine->SceneSchemas = &Schemas;
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(Owner.AttachSession(App->Session, *Engine));
        App->Session.EndMutationPhase();
    }

    void TearDown() override
    {
        // A failed assertion must never leave the worker blocked on fixture data.
        ReadGate.Release.store(true, std::memory_order_release);
        FinishOpen();
        App->OnClosing();
        App->OnWorldShutdown(*Engine);
        App->Session.Shutdown();
        Owner.Session     = nullptr;
        App->CurrentScene = nullptr;
        if (OwnAssets)
        {
            ZEngine::Managers::AssetManager::Shutdown();
        }
        Tetragrama::Components::Recent() = std::move(PreviousRecent);
        ECS.Shutdown();
        ReadGate.Shutdown();
        VFS.Shutdown();
        std::error_code ignored = {};
        if (!Temporary.empty())
        {
            std::filesystem::remove_all(Temporary, ignored);
        }
    }

    void Edit(float x)
    {
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        const auto target = App->Session.TargetFor(Entity);
        ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = x;
        ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
        ASSERT_TRUE(App->Session.Commit());
        App->Session.EndMutationPhase();
    }

    bool Save(const VFSPath& path)
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const auto result = SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, path, &Diagnostics);
        App->Session.EndMutationPhase();
        return result;
    }

    std::string SavedSource(const VFSPath& path)
    {
        SceneDocument document = {};
        EXPECT_TRUE(SceneDocument::Read(VFS, path, Schemas, &document, &Diagnostics));
        std::string source = {};
        EXPECT_TRUE(document.Emit(&source, &Diagnostics));
        return source;
    }

    bool Exists(const VFSPath& path)
    {
        const auto result = VFS.Exists(path);
        EXPECT_TRUE(result.Succeeded());
        return result.Succeeded() && result.Value();
    }

    bool ProcessSave()
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const bool result = App->Session.ProcessRequests() && App->ProcessSceneSave(*Engine);
        App->Session.EndMutationPhase();
        return result;
    }

    bool ProcessNewScene()
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const bool result = App->Session.ProcessRequests() && App->ProcessNewScene(*Engine);
        App->Session.EndMutationPhase();
        return result;
    }

    bool ProcessClose()
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const bool result = App->Session.ProcessRequests() && App->ProcessCloseRequest(*Engine);
        App->Session.EndMutationPhase();
        return result;
    }

    void LoadSlots()
    {
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaA, "SceneIO/A");
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaB, "SceneIO/B");
    }

    bool Prepare(std::unique_ptr<EditorSceneCandidate>* candidate)
    {
        const auto*           owner    = static_cast<const EditorScene*>(App->CurrentScene);
        SceneDocumentSettings settings = {.UUID = owner->UUID, .Name = "Replacement scene"};
        SceneDocument         document = {};
        return SceneDocument::Capture(*Engine->Scene, settings, Schemas, &document, &Diagnostics) && EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, {}, candidate, &Diagnostics);
    }

    bool Publish(std::unique_ptr<EditorSceneCandidate>& candidate, uint64_t epoch)
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const auto result = App->PublishSceneCandidate(*Engine, candidate, epoch);
        App->Session.EndMutationPhase();
        return result;
    }

    bool ProcessOpenIn(ZEngine::EngineContext& engine)
    {
        if (!App->Session.BeginMutationPhase())
        {
            return false;
        }
        const auto result = App->ProcessSceneOpen(engine);
        App->Session.EndMutationPhase();
        return result;
    }

    bool ProcessOpen()
    {
        return ProcessOpenIn(*Engine);
    }

    void PrepareUncachedMesh()
    {
        using namespace ZEngine::Importers;
        ASSERT_EQ(ZEngine::Managers::AssetManager::Instance(), nullptr);
        AssetMemory.Initialize(ZMega(512), {});
        ZEngine::Managers::AssetManager::Initialize(&AssetMemory.MainArena, nullptr, Workspace.c_str());
        OwnAssets       = true;
        const auto uuid = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
        AssetMesh  mesh = {};
        mesh.MeshUUID   = uuid;
        mesh.Vertices.init(&AssetMemory.MainArena, 24, 24);
        mesh.Vertices[8] = mesh.Vertices[17] = 1.f;
        mesh.Indices.init(&AssetMemory.MainArena, 3);
        mesh.Indices.push(0);
        mesh.Indices.push(1);
        mesh.Indices.push(2);
        mesh.SubMeshes.init(&AssetMemory.MainArena, 1);
        mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        AssetNodeHierarchy hierarchy = {};
        hierarchy.MeshUUID           = uuid;
        hierarchy.NodeHierarchyUUID  = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
        hierarchy.Hierarchies.init(&AssetMemory.MainArena, 1);
        hierarchy.LocalTransforms.init(&AssetMemory.MainArena, 1);
        hierarchy.GlobalTransforms.init(&AssetMemory.MainArena, 1);
        hierarchy.Names.init(&AssetMemory.MainArena, 0);
        hierarchy.MaterialNames.init(&AssetMemory.MainArena, 0);
        hierarchy.NodeNames.init(&AssetMemory.MainArena, 16);
        hierarchy.NodeMeshes.init(&AssetMemory.MainArena, 16);
        hierarchy.NodeMaterials.init(&AssetMemory.MainArena, 16);
        AddNode(hierarchy, -1, 0);
        hierarchy.NodeMeshes.insert(0, 0);
        AssetCodec::ImportConfiguration config = {};
        config.VFS                             = &VFS;
        config.OutputAssetsPath.init(&AssetMemory.MainArena, "/Meshes");
        config.OutputAssetFile.init(&AssetMemory.MainArena, "triangle.zemesh");
        ASSERT_TRUE(AssetCodec::SerializeMeshAssetFile(&AssetMemory.MainArena, mesh, hierarchy, config).Succeeded());
        ASSERT_TRUE(ZEngine::Managers::AssetManager::Instance()->Registry->Register(uuid, ZEngine::Managers::AssetType::MESH, VFSPath::Parse("/Meshes/triangle.zemesh").Value(), {}).IsOk());
        ECS.AddComponent<MeshComponent>(Entity, {uuid, 12345});
    }

    bool StartBlockedOpen()
    {
        ReadGate.Initialize(&Memory.MainArena);
        if (ReadGate.Mount(&Backend, VFSPath::Root(), 0).Failed())
        {
            return false;
        }
        Engine->VFS = &ReadGate;
        if (!App->RequestSceneOpen(Path, App->Session.SceneEpoch()) || !ProcessOpen())
        {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ReadGate.Started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        return ReadGate.Started.load(std::memory_order_acquire);
    }

    bool CompleteBlockedRead()
    {
        ReadGate.Release.store(true, std::memory_order_release);
        auto* pool = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
        if (!pool)
        {
            return false;
        }
        const auto count = pool->WorkerCount;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!pool->SubmitToWorker(i, &CompletedFences, [](void* context) { static_cast<std::atomic<uint32_t>*>(context)->fetch_add(1, std::memory_order_release); }))
            {
                ZEngine::Helpers::ThreadPoolHelper::Shutdown();
                ZEngine::Helpers::ThreadPoolHelper::Initialize();
                return false;
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (CompletedFences.load(std::memory_order_acquire) != count && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (CompletedFences.load(std::memory_order_acquire) == count)
        {
            return true;
        }
        // Fences are fixture-owned; join before a failed assertion can retire them.
        ZEngine::Helpers::ThreadPoolHelper::Shutdown();
        ZEngine::Helpers::ThreadPoolHelper::Initialize();
        return false;
    }

    void CheckStaleOpenOwner(SceneOpenOwnerChange change, bool completed)
    {
        LoadSlots();
        Edit(3.f);
        ASSERT_TRUE(Save(Path));
        const auto epoch = App->Session.SceneEpoch();
        ASSERT_TRUE(StartBlockedOpen());
        if (completed)
        {
            ASSERT_TRUE(CompleteBlockedRead());
        }

        // An invalid completion must not cancel a newer preview in the old scene.
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        const auto target = App->Session.TargetFor(Entity);
        ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
        LocalTransformValue preview = {};
        preview.Position.x          = 8.f;
        ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
        App->Session.EndMutationPhase();

        SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &Diagnostics));
        Scene        other_world   = {};
        ActorManager other_actors  = {};
        EditorScene  other_owner   = {};
        other_owner.Session        = &App->Session;
        other_owner.UUID           = Owner.UUID;
        const auto scene_uuid      = Owner.UUID;
        const auto other_workspace = Workspace + "-other";
        auto       other_engine    = std::make_unique<ZEngine::EngineContext>();
        other_engine->VFS          = Engine->VFS;
        other_engine->SceneSchemas = Engine->SceneSchemas;
        other_engine->Scene        = Engine->Scene;
        auto* completion_engine    = Engine.get();
        switch (change)
        {
            case SceneOpenOwnerChange::VFS:
                Engine->VFS = &VFS;
                break;
            case SceneOpenOwnerChange::Schemas:
                Engine->SceneSchemas = &schemas;
                break;
            case SceneOpenOwnerChange::Workspace:
                App->WorkingSpacePath = other_workspace.c_str();
                break;
            case SceneOpenOwnerChange::EmptyWorkspace:
                App->WorkingSpacePath = "";
                break;
            case SceneOpenOwnerChange::NullWorkspace:
                App->WorkingSpacePath = nullptr;
                break;
            case SceneOpenOwnerChange::RenderOwner:
                App->CurrentScene = &other_owner;
                break;
            case SceneOpenOwnerChange::SceneUUID:
                Owner.UUID = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
                break;
            case SceneOpenOwnerChange::World:
                Engine->Scene = &other_world;
                break;
            case SceneOpenOwnerChange::Actors:
                Engine->ActorManager = &other_actors;
                break;
            case SceneOpenOwnerChange::Engine:
                completion_engine = other_engine.get();
                break;
        }
        // No fatal assertions until all temporary owners have been restored.
        EXPECT_FALSE(ProcessOpenIn(*completion_engine));
        EXPECT_EQ(App->SceneOpenPending(), !completed);
        EXPECT_TRUE(App->Session.Active());
        EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 8.f);
        EXPECT_EQ(App->Session.SceneEpoch(), epoch);
        EXPECT_EQ(App->Session.History().UndoCount(), 1u);
        EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
        EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
        Engine->VFS           = &ReadGate;
        Engine->SceneSchemas  = &Schemas;
        Engine->Scene         = &ECS;
        Engine->ActorManager  = nullptr;
        App->CurrentScene     = &Owner;
        App->WorkingSpacePath = Workspace.c_str();
        Owner.UUID            = scene_uuid;

        // Restoring a pointer/value must not re-arm work already observed stale.
        ReadGate.Release.store(true, std::memory_order_release);
        FinishOpen();
        EXPECT_EQ(Engine->Scene, &ECS);
        EXPECT_EQ(App->CurrentScene, &Owner);
        EXPECT_EQ(App->Session.SceneEpoch(), epoch);
        EXPECT_EQ(App->Session.History().UndoCount(), 1u);
        EXPECT_TRUE(App->Session.Active());
        EXPECT_TRUE(ECS.IsAlive(Entity));
        EXPECT_FALSE(App->SceneChangePromptState().Visible());
        EXPECT_EQ(Tetragrama::Components::Recent().Count, 0);
    }

    bool WaitForOpenPrompt()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (App->SceneOpenPending() && std::chrono::steady_clock::now() < deadline)
        {
            if (App->SceneChangePromptState().Visible())
            {
                return true;
            }
            (void) ProcessOpen();
            std::this_thread::yield();
        }
        return App->SceneChangePromptState().Visible();
    }

    bool Answer(SceneChangeChoice choice)
    {
        return App->ChooseSceneChange(choice, App->SceneChangePromptState().Serial);
    }

    void FinishOpen()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (App->SceneOpenPending() && std::chrono::steady_clock::now() < deadline)
        {
            const auto prompt = App->SceneChangePromptState();
            if (prompt.Visible())
            {
                (void) App->ChooseSceneChange(SceneChangeChoice::Cancel, prompt.Serial);
            }
            (void) ProcessOpen();
            std::this_thread::yield();
        }
        // A failed timing assertion must not free a task context a worker still owns.
        if (App->SceneOpenPending())
        {
            ADD_FAILURE() << "Scene open did not complete within five seconds";
            ZEngine::Helpers::ThreadPoolHelper::Shutdown();
            App->OnWorldShutdown(*Engine);
            ZEngine::Helpers::ThreadPoolHelper::Initialize();
        }
    }
};

// Real event/modifier dispatch, bounded scene intents and a mock native picker.
// No OS window, GPU, overlay rebuild or scheduler callback is needed.
class EditorSceneFileInputTest : public EditorSceneIOTest
{
protected:
    ZEngine::UI::ZUIContext                                   Context  = {};
    std::unique_ptr<ZEngine::Applications::AppRenderPipeline> Pipeline = {};
    Tetragrama::Layers::ZUILayer                              Layer    = {};

    void                                                      SetUp() override
    {
        EditorSceneIOTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        Pipeline         = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
        Pipeline->ZUICtx = &Context;
        Pipeline->OverlayBuildAvailable.value.store(false);
        App->RenderPipeline = Pipeline.get();
        App->CurrentWindow  = nullptr; // The mock picker is not a GLFW window.
        Layer.Initialize(&Memory.MainArena, App.get());
        App->CurrentWindow = &Picker;
        App->ZUIUILayer    = &Layer;
    }

    void TearDown() override
    {
        if (App)
        {
            App->ZUIUILayer     = nullptr;
            App->RenderPipeline = nullptr;
        }
        Layer.LocalArena.Shutdown();
        EditorSceneIOTest::TearDown();
    }

    void Key(ZENGINE_KEYCODE code, int repeat = 0)
    {
        ZEngine::Windows::Events::KeyPressedEvent event{code, repeat};
        App->ProcessEvent(event);
    }

    void Release(ZENGINE_KEYCODE code)
    {
        ZEngine::Windows::Events::KeyReleasedEvent event{code};
        App->ProcessEvent(event);
    }

    void ShortcutModifier()
    {
#if defined(__APPLE__)
        Key(ZENGINE_KEY_LEFT_SUPER);
#else
        Key(ZENGINE_KEY_LEFT_CONTROL);
#endif
    }

    void Update(bool focused = true)
    {
        App->UpdateSessionInput(&Context, focused, Engine.get());
    }

    void FileKeys()
    {
        Key(ZENGINE_KEY_N);
        Key(ZENGINE_KEY_O);
        Key(ZENGINE_KEY_S);
        Key(ZENGINE_KEY_LEFT_SHIFT);
        Key(ZENGINE_KEY_S);
        Release(ZENGINE_KEY_LEFT_SHIFT);
    }

    void CheckShortcutOwnerChange(ZENGINE_KEYCODE code, bool shift = false)
    {
        Edit(3.f);
        ShortcutModifier();
        if (shift)
        {
            Key(ZENGINE_KEY_LEFT_SHIFT);
        }
        Key(code);
        const auto uuid     = Owner.UUID;
        const auto epoch    = App->Session.SceneEpoch();
        const auto revision = App->Session.DocumentRevision();
        Owner.UUID          = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
        Update();
        Owner.UUID = uuid;
        EXPECT_EQ(Picker.Requests, 0u);
        EXPECT_EQ(Picker.OpenRequests, 0u);
        EXPECT_FALSE(App->SceneChangePromptState().Visible());
        EXPECT_EQ(App->Session.SceneEpoch(), epoch);
        EXPECT_EQ(App->Session.DocumentRevision(), revision);
        EXPECT_EQ(App->Session.History().UndoCount(), 1u);
        EXPECT_TRUE(Owner.IsDirty());
        EXPECT_TRUE(ECS.IsAlive(Entity));
        Update(); // Restoring the original owner must not rearm the edge.
        EXPECT_EQ(Picker.Requests, 0u);
        EXPECT_EQ(Picker.OpenRequests, 0u);
        EXPECT_FALSE(App->SceneChangePromptState().Visible());
        Key(code); // A new physical edge must still work for the current owner.
        Update();
        if (code == ZENGINE_KEY_N)
        {
            const auto prompt = App->SceneChangePromptState();
            ASSERT_TRUE(prompt.Visible());
            ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Cancel, prompt.Serial));
        }
        else if (code == ZENGINE_KEY_O)
        {
            ASSERT_EQ(Picker.OpenRequests, 1u);
            ASSERT_TRUE(Picker.OpenResult.Complete({}));
            App->PollSceneOpenDialog();
        }
        else
        {
            ASSERT_EQ(Picker.Requests, 1u);
            ASSERT_TRUE(Picker.Result.Complete({}));
            App->PollSceneSaveDialog();
        }
        Update();
        EXPECT_TRUE(ECS.IsAlive(Entity));
        EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    }
};

class EditorSceneDragFileInputTest : public EditorSceneFileInputTest
{
protected:
    struct SceneCamera : ZEngine::Controllers::FlyCameraController
    {
        mutable ZEngine::Rendering::Cameras::FlyCamera Camera{4.f / 3.f, {}};
        ZEngine::Rendering::Cameras::CameraPtr         GetCamera() const override
        {
            return &Camera;
        }
    } Camera = {};

    void SetUp() override
    {
        EditorSceneFileInputTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        ZEngine::UI::ZUIContextInit(&Context, &Memory.MainArena, ZKilo(128), ZKilo(64), 64, 32);
        Camera.Camera.SetPosition({0.f, 0.f, 10.f});
        Camera.Camera.SetOrientation(0.f, 0.f);
        App->CameraController  = &Camera;
        Picker.ObservedSession = &App->Session;
    }

    void TearDown() override
    {
        if (App)
        {
            App->GizmoViewport.Unbind();
            App->CameraController = nullptr;
        }
        ZEngine::UI::ZUIContextDestroy(&Context);
        EditorSceneFileInputTest::TearDown();
    }

    void BeginControl()
    {
        ZEngine::UI::ZUIFeedMousePos(&Context, 100.f, 100.f);
        ZEngine::UI::ZUIFeedMouseButton(&Context, 0, true);
        Context.ActiveKey = 42;
        ASSERT_TRUE(App->Session.RequestControlDrag(Entity, {.Key = 42, .PointerSerial = Context.MousePressSerial[0], .StartX = 100.f, .UnitsPerPixel = 1.f}));
        Update();
        ASSERT_TRUE(App->Session.Active());
        ZEngine::UI::ZUIFeedMousePos(&Context, 109.f, 100.f);
        Update();
        ASSERT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    }

    void BeginViewport()
    {
        using namespace ZEngine::UI;
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(App->Session.Select(Entity));
        ASSERT_TRUE(App->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Translate}));
        ASSERT_TRUE(App->Session.ProcessRequests());
        App->Session.EndMutationPhase();
        ZUIBeginFrame(&Context, 1.f / 60.f);
        auto* root         = ZUIPushBox(&Context, "", 0, ZUI_None);
        root->Key          = 1;
        root->ScreenMax[0] = 1024.f;
        root->ScreenMax[1] = 768.f;
        const float rect[] = {37.f, 61.f, 837.f, 661.f};
        for (uint64_t key : {42u, 99u})
        {
            Context.Current   = root;
            auto* box         = ZUIPushBox(&Context, "", 0, ZUI_Clickable | ZUI_NoKeyboardFocus);
            box->Key          = key;
            box->ScreenMin[0] = rect[0];
            box->ScreenMin[1] = rect[1];
            box->ScreenMax[0] = rect[2];
            box->ScreenMax[1] = rect[3];
            if (key == 99)
            {
                box->CustomHitTest     = Gizmo::Viewport::Filter;
                box->CustomHitTestData = &App->GizmoViewport;
            }
        }
        Context.ViewportInputKey = Context.HotKey = 42;
        ZUIFeedMousePos(&Context, 437.f, 361.f);
        App->GizmoViewport.Bind(&Context, rect, 42, 99);
        App->RefreshGizmoHitFrame(&Context);
        ZUIFeedMouseButton(&Context, 0, true);
        Update();
        ASSERT_TRUE(App->Session.TranslationActive());
        ASSERT_TRUE(App->GizmoViewport.OwnsPointerCapture(App->Session, &Context));
        ZUIFeedMousePos(&Context, 467.f, 361.f);
        Update();
        ASSERT_NE(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    }

    void ExpectCancelledOpen()
    {
        EXPECT_EQ(Picker.OpenRequests, 1u);
        EXPECT_FALSE(Picker.ActiveAtOpen);
        EXPECT_FALSE(App->Session.Active());
        EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
        EXPECT_EQ(App->Session.History().UndoCount(), 0u);
        EXPECT_FALSE(Owner.IsDirty());
        EXPECT_EQ(Context.ActiveKey, 0u);
        EXPECT_FALSE(Context.MousePressed[0]);
        EXPECT_FALSE(Context.InteractionCapture.Keyboard);
        EXPECT_FALSE(Context.InteractionCapture.Pointer);
    }
};

TEST_F(EditorSceneDragFileInputTest, OpenCancelsInspectorBeforeSamePollReleaseAndPickerDispatch)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    EXPECT_TRUE(App->Session.Active()); // Event dispatch cannot author rollback.
    EXPECT_EQ(Picker.OpenRequests, 0u);
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    ZEngine::UI::ZUIFeedBeginFrame(&Context); // Durable release, no overlay.
    Update();
    ExpectCancelledOpen();
    ASSERT_TRUE(Picker.OpenResult.Complete({}));
    App->PollSceneOpenDialog();
    Update();
    EXPECT_EQ(Picker.OpenRequests, 1u);
}

TEST_F(EditorSceneDragFileInputTest, OpenCancelsViewportBeforeSamePollReleaseWithoutRearmingHeldInput)
{
    BeginViewport();
    ASSERT_FALSE(HasFatalFailure());
    const auto frame = Context.FrameIndex;
    const auto bytes = Context.FrameArena.m_current_offset;
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    ZEngine::UI::ZUIFeedBeginFrame(&Context);
    Update();
    ExpectCancelledOpen();
    EXPECT_FALSE(App->GizmoViewport.OwnsPointerCapture(App->Session, &Context));
    ASSERT_TRUE(Picker.OpenResult.Complete({}));
    App->PollSceneOpenDialog();
    Update();
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(Context.FrameIndex, frame);
    EXPECT_EQ(Context.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorSceneDragFileInputTest, CancellingOpenPreservesRedoAndAllowsAFreshPhysicalInspectorDrag)
{
    Edit(3.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    App->Session.EndMutationPhase();
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    Update();
    ExpectCancelledOpen();
    EXPECT_EQ(App->Session.History().RedoCount(), 1u);
    ASSERT_TRUE(Picker.OpenResult.Complete({}));
    App->PollSceneOpenDialog();
    Update(); // The original held pointer must not replay.
    EXPECT_FALSE(App->Session.Active());
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(App->Session.History().RedoCount(), 0u);
}

TEST_F(EditorSceneDragFileInputTest, SaveSaveAsAndNewNeverInterruptAnOwnedPreviewOrReplayAfterCancel)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_S);
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_S);
    Release(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_N);
    Context.EscapePressed = true;
    Update();
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
}

TEST_F(EditorSceneDragFileInputTest, ANewPhysicalPressRetiresOpenInsteadOfCancellingTheNewDrag)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    App->Session.Cancel();
    App->Session.EndMutationPhase();
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, true);
    ASSERT_TRUE(App->Session.RequestControlDrag(Entity, {.Key = 42, .PointerSerial = Context.MousePressSerial[0], .StartX = 100.f, .UnitsPerPixel = 1.f}));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.ProcessRequests());
    App->Session.EndMutationPhase();
    ASSERT_EQ(App->Session.PendingRequestCount(), 0u); // Exercise serial revalidation, not the pending-work guard.
    Update();
    EXPECT_TRUE(App->Session.Active());
    EXPECT_EQ(Picker.OpenRequests, 0u);
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorSceneDragFileInputTest, PendingAuthoredWorkRetiresTheInterruptRatherThanOpeningBehindIt)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    ASSERT_TRUE(App->Session.RequestSelection(Entity));
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    Context.EscapePressed = true;
    Update();
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
}

TEST_F(EditorSceneDragFileInputTest, AChangedWidgetOwnerRetiresOpenWithoutClearingTheNewOwner)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    Context.ActiveKey = 1001;
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_EQ(Context.ActiveKey, 1001u);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    Context.ActiveKey = 0;
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
}

TEST_F(EditorSceneDragFileInputTest, TextOwnershipBlocksOpenEvenDuringAnOwnedInspectorDrag)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Context.TextInputActive = true;
    Key(ZENGINE_KEY_O);
    Context.TextInputActive = false;
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_TRUE(App->Session.Active());
    Context.EscapePressed = true;
    Update();
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
}

TEST_F(EditorSceneDragFileInputTest, BlockingOwnersRejectAndRetireAnInterruptAtBothInputBoundaries)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    for (bool after_event : {false, true})
    {
        for (uint32_t blocker = 0; blocker < 8; ++blocker)
        {
            if (after_event)
            {
                Key(ZENGINE_KEY_O);
            }
            Context.TextInputActive   = blocker == 0;
            Context.DragSourceKey     = blocker == 1 ? 1001 : 0;
            Context.ActiveModalKey    = blocker == 2 ? 1001 : 0;
            Context.PendingPopupKey   = blocker == 3 ? 1001 : 0;
            Context.PopupStackSize    = blocker == 4 ? 1 : 0;
            Context.MouseDown[1]      = blocker == 5;
            Context.MouseDown[2]      = blocker == 6;
            ZEngine::UI::ZUIBox modal = {};
            Context.ModalBox          = blocker == 7 ? &modal : nullptr;
            if (!after_event)
            {
                Key(ZENGINE_KEY_O);
            }
            Update();
            EXPECT_EQ(Picker.OpenRequests, 0u);
            EXPECT_TRUE(App->Session.Active());
            Context.TextInputActive = false;
            Context.DragSourceKey = Context.ActiveModalKey = Context.PendingPopupKey = 0;
            Context.PopupStackSize                                                   = 0;
            Context.MouseDown[1] = Context.MouseDown[2] = false;
            Context.ModalBox                            = nullptr;
            Update();
            EXPECT_EQ(Picker.OpenRequests, 0u);
        }
    }
}

TEST_F(EditorSceneDragFileInputTest, FocusLossRetiresTheInterruptInsteadOfOpeningOnReactivation)
{
    BeginViewport();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    ZEngine::Windows::Events::WindowFocusLostEvent lost = {};
    App->ProcessEvent(lost);
    Update(false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    EXPECT_EQ(Context.ActiveKey, 0u);
}

TEST_F(EditorSceneDragFileInputTest, ReplacementEpochRetiresTheCapturedDragOpen)
{
    BeginControl();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&ECS));
    App->Session.EndMutationPhase();
    Update();
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
}

TEST_F(EditorSceneDragFileInputTest, SelectingASceneAfterViewportCancellationCannotWriteThroughTheOldCapture)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    const auto source = SavedSource(Path);
    const auto epoch  = App->Session.SceneEpoch();
    BeginViewport();
    ASSERT_FALSE(HasFatalFailure());
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    Update();
    ExpectCancelledOpen();
    EXPECT_EQ(SavedSource(Path), source);
    ASSERT_TRUE(Picker.OpenResult.Complete((Temporary / "project" / "Scenes" / "main.zescene").string()));
    App->PollSceneOpenDialog();
    FinishOpen();
    ASSERT_NE(Engine->Scene, &ECS);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    const auto loaded = App->Session.Resolve(*uuids::uuid::from_string("22222222-2222-4222-8222-222222222222"));
    ASSERT_TRUE(Engine->Scene->IsAlive(loaded));
    ZEngine::UI::ZUIFeedMousePos(&Context, 497.f, 361.f);
    ZEngine::UI::ZUIFeedMouseButton(&Context, 0, false);
    Update();
    EXPECT_FLOAT_EQ(Engine->Scene->GetComponent<TransformComponent>(loaded)->Position.x, 0.f);
    EXPECT_FALSE(ECS.IsAlive(Entity));
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(SavedSource(Path), source);
}

TEST_F(EditorSceneFileInputTest, NewShortcutRetiresASameEpochForeignOwnerInsteadOfTransferringItsIntent)
{
    CheckShortcutOwnerChange(ZENGINE_KEY_N);
}

TEST_F(EditorSceneFileInputTest, OpenShortcutRetiresASameEpochForeignOwnerInsteadOfTransferringItsIntent)
{
    CheckShortcutOwnerChange(ZENGINE_KEY_O);
}

TEST_F(EditorSceneFileInputTest, SaveShortcutRetiresASameEpochForeignOwnerInsteadOfTransferringItsIntent)
{
    CheckShortcutOwnerChange(ZENGINE_KEY_S);
}

TEST_F(EditorSceneFileInputTest, SaveAsShortcutRetiresASameEpochForeignOwnerInsteadOfTransferringItsIntent)
{
    CheckShortcutOwnerChange(ZENGINE_KEY_S, true);
}

TEST_F(EditorSceneFileInputTest, SaveAsIsOneShotAndDeferredUntilTheMutationPointWithoutAnOverlay)
{
    Edit(3.f);
    ShortcutModifier();
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_S);
    EXPECT_EQ(Picker.Requests, 0u);
    Key(ZENGINE_KEY_S, 1);
    Update();
    ASSERT_EQ(Picker.Requests, 1u);
    for (uint32_t update = 0; update < 8; ++update)
    {
        Key(ZENGINE_KEY_S, 1);
        Update();
    }
    EXPECT_EQ(Picker.Requests, 1u);
    ASSERT_TRUE(Picker.Result.Complete((Temporary / "project" / "Scenes" / "main.zescene").string()));
    App->PollSceneSaveDialog();
    EXPECT_FALSE(Exists(Path));
    Update();
    EXPECT_TRUE(Exists(Path));
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_EQ(Owner.SourcePath, Path);
}

TEST_F(EditorSceneFileInputTest, SaveCapturesEarlierQueuedEditsAndUnnamedSaveUsesThePicker)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_S);
    Update();
    ASSERT_EQ(Picker.Requests, 1u);
    ASSERT_TRUE(Picker.Result.Complete({}));
    App->PollSceneSaveDialog();
    ASSERT_TRUE(Save(Path));
    LocalTransformValue value = {};
    value.Position.x          = 7.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, value));
    Key(ZENGINE_KEY_S);
    EXPECT_FALSE(Owner.IsDirty());
    Update();
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 7.f);
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_EQ(Picker.Requests, 1u);
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &document, &Diagnostics));
    Scene candidate = {};
    candidate.Initialize(&Memory.MainArena);
    ASSERT_TRUE(document.PopulateCandidate(candidate, Schemas, &Diagnostics));
    candidate.ForEach<TransformComponent>([](EntityID, const TransformComponent& transform) { EXPECT_FLOAT_EQ(transform.Position.x, 7.f); });
    candidate.Shutdown();
}

TEST_F(EditorSceneFileInputTest, OpenIsDeferredAndAnOutstandingPickerExcludesOtherFileShortcuts)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_O, 1);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    Key(ZENGINE_KEY_O);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    Update();
    ASSERT_EQ(Picker.OpenRequests, 1u);
    EXPECT_EQ(Picker.OpenDirectory, Workspace + PLATFORM_OS_BACKSLASH + "Scenes");
    FileKeys();
    Update();
    EXPECT_EQ(Picker.OpenRequests, 1u);
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    ASSERT_TRUE(Picker.OpenResult.Complete({}));
    App->PollSceneOpenDialog();
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, NewUsesExistingDirtyConsentAndNeverReplaysAfterCancellationOrDiscard)
{
    Edit(5.f);
    ShortcutModifier();
    Key(ZENGINE_KEY_N);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    Update();
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 5.f);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    Update();
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_TRUE(ECS.IsAlive(Entity));
    Update();
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    Key(ZENGINE_KEY_N);
    Update();
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    Update();
    EXPECT_EQ(ECS.AliveCount(), 0u);
    const auto epoch = App->Session.SceneEpoch();
    Key(ZENGINE_KEY_N, 1);
    Update();
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneFileInputTest, OnlyAdvertisedModifierCombinationsAreAcceptedAndReleaseDoesNotLoseAnEdge)
{
    FileKeys(); // No shortcut modifier.
    Update();
    ShortcutModifier();
    Key(ZENGINE_KEY_LEFT_ALT);
    FileKeys();
    Release(ZENGINE_KEY_LEFT_ALT);
    Update();
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_N);
    Key(ZENGINE_KEY_O);
    Release(ZENGINE_KEY_LEFT_SHIFT);
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    Key(ZENGINE_KEY_S);
    Layer.ResetInput(); // Modifier/key release is not focus loss.
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, WidgetModalPopupAndPointerOwnersRejectEventsWithoutDelayedReplay)
{
    ShortcutModifier();
    bool ZEngine::UI::ZUIContext::* bool_owners[] = {&ZEngine::UI::ZUIContext::TextInputActive};
    for (auto field : bool_owners)
    {
        Context.*field = true;
        FileKeys();
        Context.*field = false;
        Update();
    }
    uint64_t ZEngine::UI::ZUIContext::* key_owners[] = {&ZEngine::UI::ZUIContext::ActiveKey, &ZEngine::UI::ZUIContext::DragSourceKey, &ZEngine::UI::ZUIContext::ActiveModalKey, &ZEngine::UI::ZUIContext::PendingPopupKey};
    for (auto field : key_owners)
    {
        Context.*field = 42;
        FileKeys();
        Context.*field = 0;
        Update();
    }
    Context.PopupStackSize = 1;
    FileKeys();
    Context.PopupStackSize = 0;
    Update();
    ZEngine::UI::ZUIBox modal = {};
    Context.ModalBox          = &modal;
    FileKeys();
    Context.ModalBox = nullptr;
    Update();
    Context.InteractionCapture.Keyboard = true;
    FileKeys();
    Context.InteractionCapture = {};
    Update();
    for (uint32_t button = 0; button < 3; ++button)
    {
        Context.MouseDown[button] = true;
        FileKeys();
        Context.MouseDown[button] = false;
        Update();
    }
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneFileInputTest, OwnershipAcquiredAfterTheEventRetiresRatherThanDefersTheCommand)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_LEFT_SHIFT);
    for (uint32_t owner = 0; owner < 5; ++owner)
    {
        Key(ZENGINE_KEY_S);
        Context.TextInputActive             = owner == 0;
        Context.ActiveModalKey              = owner == 1 ? 42 : 0;
        Context.PopupStackSize              = owner == 2 ? 1 : 0;
        Context.ActiveKey                   = owner == 3 ? 42 : 0;
        Context.InteractionCapture.Keyboard = owner == 4;
        Update();
        Context.TextInputActive = false;
        Context.ActiveModalKey = Context.ActiveKey = 0;
        Context.PopupStackSize                     = 0;
        Context.InteractionCapture                 = {};
        Update();
        EXPECT_EQ(Picker.Requests, 0u);
    }
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, TransientFocusLossAndMinimizationRetireOldAndNewShortcutEdges)
{
    for (bool minimize : {false, true})
    {
        ShortcutModifier();
        Key(ZENGINE_KEY_S);
        if (minimize)
        {
            ZEngine::Windows::Events::WindowMinimizedEvent lost = {};
            App->ProcessEvent(lost);
        }
        else
        {
            ZEngine::Windows::Events::WindowFocusLostEvent lost = {};
            App->ProcessEvent(lost);
        }
        ZEngine::Windows::Events::WindowFocusGainedEvent gained = {};
        App->ProcessEvent(gained);
        ShortcutModifier();
        FileKeys();
        Update();
        Update();
        EXPECT_EQ(Picker.Requests, 0u);
        EXPECT_EQ(Picker.OpenRequests, 0u);
        EXPECT_TRUE(ECS.IsAlive(Entity));
    }
    ShortcutModifier();
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, ReplacementEpochAndFailedMutationEntryCannotReplayAStaleCommand)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_S);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    Update(); // A nested phase cannot consume the queued intent.
    EXPECT_EQ(Picker.Requests, 0u);
    ASSERT_TRUE(App->Session.BindScene(&ECS));
    App->Session.EndMutationPhase();
    Update();
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, ActiveOrNewlyQueuedPreviewCannotBeSavedOrReplayedAfterCancellation)
{
    ASSERT_TRUE(Save(Path));
    ShortcutModifier();
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
    App->Session.EndMutationPhase();
    FileKeys();
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    App->Session.Cancel();
    App->Session.EndMutationPhase();
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    Key(ZENGINE_KEY_S);
    ASSERT_TRUE(App->Session.RequestControlDrag(Entity, {.Key = 42, .UnitsPerPixel = 1.f}));
    Context.ActiveKey    = 42;
    Context.MouseDown[0] = true;
    Update();
    ASSERT_TRUE(App->Session.Active());
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    App->Session.Cancel();
    App->Session.EndMutationPhase();
    Context.ActiveKey    = 0;
    Context.MouseDown[0] = false;
    Update();
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    EXPECT_EQ(Picker.Requests, 0u);
}

TEST_F(EditorSceneFileInputTest, FirstAcceptedFileEdgeWinsWithoutUnboundedCallbackOrIntentGrowth)
{
    ShortcutModifier();
    const auto arena_before = Memory.MainArena.m_current_offset;
    Key(ZENGINE_KEY_O);
    for (uint32_t press = 0; press < 1000; ++press)
    {
        FileKeys();
    }
    EXPECT_EQ(App->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(Memory.MainArena.m_current_offset, arena_before);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 1u);
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneFileInputTest, ReadOnlyAndClosingScenesNeverStartASavePicker)
{
    ShortcutModifier();
    Owner.SourceReadOnly = true;
    Key(ZENGINE_KEY_S);
    Update();
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    Owner.SourceReadOnly = false;
    Key(ZENGINE_KEY_S);
    App->OnClosing();
    Update();
    FileKeys();
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
}

TEST_F(EditorSceneFileInputTest, ReadOnlySaveEdgesNeverRearmWhenTheDocumentBecomesEditable)
{
    ShortcutModifier();
    Owner.SourceReadOnly = true;
    Key(ZENGINE_KEY_S);
    Owner.SourceReadOnly = false;
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    Owner.SourceReadOnly = true;
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_S);
    Owner.SourceReadOnly = false;
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    Key(ZENGINE_KEY_S);
    Update();
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneFileInputTest, ReadOnlyScenesCanStillRequestOpenAndConfirmedNew)
{
    ShortcutModifier();
    Owner.SourceReadOnly = true;
    Key(ZENGINE_KEY_O);
    Update();
    ASSERT_EQ(Picker.OpenRequests, 1u);
    ASSERT_TRUE(Picker.OpenResult.Complete({}));
    App->PollSceneOpenDialog();
    Key(ZENGINE_KEY_N);
    Update();
    EXPECT_EQ(ECS.AliveCount(), 0u); // The existing clean-scene lifecycle permits replacement.
    EXPECT_EQ(Picker.Requests, 0u);
}

TEST_F(EditorSceneFileInputTest, UnfocusedUpdateAndCloseBoundaryRetireCommandsWithoutOpeningPickers)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_S);
    Update(false);
    Update();
    EXPECT_EQ(Picker.Requests, 0u);
    ShortcutModifier();
    Key(ZENGINE_KEY_O);
    Engine->CloseRequested.value.store(true, std::memory_order_release);
    Update();
    Engine->CloseRequested.value.store(false, std::memory_order_release);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 0u);
    Key(ZENGINE_KEY_O);
    Update();
    EXPECT_EQ(Picker.OpenRequests, 1u);
}

TEST_F(EditorSceneIOTest, SuccessfulSaveOwnsSceneSettingsAndAdvancesCheckpointWithoutAddingHistory)
{
    Edit(12.f);
    Owner.AuthoredGrid.Origin = {1.f, 2.f, 3.f};
    Owner.MarkDirty(true);
    ASSERT_TRUE(Save(Path));
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_EQ(Owner.SourcePath, Path);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &document, &Diagnostics));
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_EQ(document.Settings()->UUID, Owner.UUID);
    EXPECT_EQ(document.Settings()->Name, "Main Scene");
    EXPECT_FLOAT_EQ(document.Settings()->Grid.Origin.y, 2.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.Redo());
    EXPECT_FALSE(Owner.IsDirty());
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneIOTest, FailedAtomicReplacementPreservesFileCheckpointAndSaveAsSourcePath)
{
    Edit(5.f);
    ASSERT_TRUE(Save(Path));
    const auto original = SavedSource(Path);
    Edit(9.f);
    VFS.FailRename = true;
    EXPECT_FALSE(Save(Path));
    EXPECT_EQ(SavedSource(Path), original);
    const auto another = VFSPath::Parse("/Scenes/other.zescene").Value();
    EXPECT_FALSE(Save(another));
    EXPECT_EQ(Owner.SourcePath, Path);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(Exists(another));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_FALSE(Owner.IsDirty()); // failed write did not move the saved checkpoint
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneIOTest, UnsupportedRuntimeComponentNeverProducesALossySaveOrClearsDirty)
{
    Edit(5.f);
    ECS.AddComponent<RigidBodyComponent>(Entity, {});
    EXPECT_FALSE(Save(Path));
    EXPECT_FALSE(Exists(Path));
    EXPECT_FALSE(Owner.SourcePath.IsValid());
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_TRUE(Diagnostics.HasErrors());
}

TEST_F(EditorSceneIOTest, ActivePreviewCannotBeSavedOrCommittedImplicitly)
{
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 8.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    EXPECT_FALSE(SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, Path, &Diagnostics));
    EXPECT_TRUE(App->Session.Active());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 8.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(Exists(Path));
    App->Session.Cancel();
    EXPECT_FALSE(Owner.IsDirty());
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneIOTest, SaveRequiresMutationOwnershipAndFullyDrainedIntent)
{
    EXPECT_FALSE(SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, Path, &Diagnostics));
    LocalTransformValue value = {};
    value.Position.x          = 3.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, value));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    EXPECT_FALSE(SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, Path, &Diagnostics));
    ASSERT_TRUE(App->Session.ProcessRequests());
    EXPECT_TRUE(SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, Path, &Diagnostics));
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneIOTest, ReadOnlyCompatibilityAndMismatchedOwnersRejectSaving)
{
    Owner.SourceReadOnly = true;
    EXPECT_FALSE(Save(Path));
    EXPECT_FALSE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    Owner.SourceReadOnly = false;
    Owner.Session        = nullptr;
    EXPECT_FALSE(Save(Path));
    EXPECT_FALSE(Exists(Path));
    Owner.Session = &App->Session;
    ASSERT_TRUE(App->Session.BeginMutationPhase(false));
    EXPECT_FALSE(SaveEditorSceneDocument(Owner, App->Session, Schemas, VFS, Path, &Diagnostics));
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneIOTest, ReadOnlyDocumentRemainsUneditableAcrossApplicationUpdates)
{
    Owner.SourceReadOnly = true;
    App->UpdateSessionInput(nullptr, true);
    EXPECT_FALSE(App->Session.Editable());
    EXPECT_FALSE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
}

TEST_F(EditorSceneIOTest, OnlyProjectSceneSourcePathsAreAccepted)
{
    for (const auto* source : {"/", "/Scenes/test.png", "/ZodiacEngine/main.zescene", "/cache/main.zescene"})
    {
        const auto path = VFSPath::Parse(source).Value();
        EXPECT_FALSE(IsSceneSourcePath(path));
        EXPECT_FALSE(Save(path));
        EXPECT_FALSE(App->RequestSceneSave(path, App->Session.SceneEpoch()));
    }
}

TEST_F(EditorSceneIOTest, NativeBoundaryAcceptsNewProjectFilesAndRejectsSiblingPrefixesOrTraversal)
{
    const auto accepted = SceneSourcePathFromNative(Workspace, (Temporary / "project" / "Scenes" / "main.zescene").string());
    ASSERT_TRUE(accepted.Succeeded());
    EXPECT_EQ(accepted.Value(), Path);
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, (Temporary / "project-other" / "main.zescene").string()).Failed());
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, (Temporary / "project" / "Scenes" / ".." / ".." / "main.zescene").string()).Failed());
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, "Scenes/main.zescene").Failed());
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, Workspace).Failed());
}

TEST_F(EditorSceneIOTest, NativeBoundaryRejectsEmbeddedNulOversizedAndNonScenePaths)
{
    std::string embedded_nul = Workspace + "/a";
    embedded_nul.push_back('\0');
    embedded_nul += "b.zescene";
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, embedded_nul).Failed());
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, std::string(VFS_MAX_PATH + 1, 'a')).Failed());
    EXPECT_TRUE(SceneSourcePathFromNative({}, (Temporary / "project" / "main.zescene").string()).Failed());
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, (Temporary / "project" / "main.png").string()).Failed());
}

TEST_F(EditorSceneIOTest, NativeBoundaryRejectsMalformedUtf8BeforeNativeConversion)
{
    const std::string_view malformed[] = {
        "\x80",
        "\xc0\xaf",
        "\xc1\xbf",
        "\xc2",
        "\xc2"
        "A",
        "\xe0\x80\xaf",
        "\xed\xa0\x80",
        "\xe2\x82",
        "\xf0\x80\x80\xaf",
        "\xf0\x9f\x92",
        "\xf4\x90\x80\x80",
        "\xf5\x80\x80\x80",
        "\xff"};
    for (const auto bytes : malformed)
    {
        const auto native = Workspace + "/Scenes/" + std::string(bytes) + ".zescene";
        EXPECT_TRUE(SceneSourcePathFromNative(Workspace, native).Failed()) << "Malformed UTF-8 byte count: " << bytes.size();
    }
}

TEST_F(EditorSceneIOTest, NativeBoundaryPreservesUtf8WorkspaceAndSceneNames)
{
    const auto      root      = Temporary / u8"\u5de5\u623f \U0001f680";
    const auto      directory = root / u8"\u5834\u9762";
    std::error_code error     = {};
    ASSERT_TRUE(std::filesystem::create_directories(directory, error));
    ASSERT_FALSE(error);
    const auto root_utf8   = root.u8string();
    const auto native_utf8 = (directory / u8"\u7167\u660e.zescene").u8string();
    const auto accepted    = SceneSourcePathFromNative(std::string(root_utf8.begin(), root_utf8.end()), std::string(native_utf8.begin(), native_utf8.end()));
    ASSERT_TRUE(accepted.Succeeded());
    EXPECT_EQ(accepted.Value(), VFSPath::Parse("/\xe5\xa0\xb4\xe9\x9d\xa2/\xe7\x85\xa7\xe6\x98\x8e.zescene").Value());
}

TEST_F(EditorSceneIOTest, ScenePickerDirectoryPreservesEveryUtf8SequenceWidth)
{
    const std::string_view names[] = {"\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80", "\xed\x9f\xbf", "\xee\x80\x80", "\xef\xbf\xbf", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"};
    for (const auto name : names)
    {
        std::string directory = {};
        ASSERT_TRUE(TrySceneDialogDirectory(Workspace, name, {}, &directory));
        EXPECT_EQ(directory, Workspace + PLATFORM_OS_BACKSLASH + std::string(name));
    }
    std::string directory = {};
    ASSERT_TRUE(TrySceneDialogDirectory(Workspace, {}, {}, &directory));
    EXPECT_EQ(directory, Workspace);
    ASSERT_TRUE(TrySceneDialogDirectory(Workspace, "ignored", VFSPath::Parse("/main.zescene").Value(), &directory));
    EXPECT_EQ(directory, Workspace);
}

TEST_F(EditorSceneIOTest, ScenePickerDirectoryFailurePreservesOutputAndNeverTruncates)
{
    std::string directory = "retained";
    for (const auto name : {std::string("a\0b", 3), std::string("\xff"), std::string("\xed\xa0\x80"), std::string(VFS_MAX_PATH - 1, 'a'), std::string(VFS_MAX_PATH, 'a')})
    {
        EXPECT_FALSE(TrySceneDialogDirectory(Workspace, name, {}, &directory));
        EXPECT_EQ(directory, "retained");
    }
    EXPECT_FALSE(TrySceneDialogDirectory({}, "Scenes", {}, &directory));
    EXPECT_FALSE(TrySceneDialogDirectory("relative", "Scenes", {}, &directory));
    EXPECT_FALSE(TrySceneDialogDirectory(Workspace, {}, VFSPath::Parse("/cache/main.zescene").Value(), &directory));
    EXPECT_FALSE(TrySceneDialogDirectory(Workspace, {}, {}, nullptr));
    EXPECT_EQ(directory, "retained");
}

TEST_F(EditorSceneIOTest, ScenePickersKeepUtf8ConfiguredAndSavedSourceDirectories)
{
    const auto root_utf8 = (Temporary / u8"\u5de5\u623f \U0001f680").u8string();
    Workspace.assign(root_utf8.begin(), root_utf8.end());
    App->WorkingSpacePath = Workspace.c_str();
    Configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
    Configuration.ScenePath.init(&Memory.MainArena, "\xe5\xa0\xb4\xe9\x9d\xa2");
    App->SaveSceneAs();
    ASSERT_EQ(Picker.Requests, 1u);
    EXPECT_EQ(Picker.SaveDirectory, Workspace + PLATFORM_OS_BACKSLASH + "\xe5\xa0\xb4\xe9\x9d\xa2");
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();

    Owner.SourcePath = VFSPath::Parse("/\xe7\xb7\xa8\xe9\x9b\x86/\xe7\x85\xa7\xe6\x98\x8e.zescene").Value();
    App->SaveSceneAs();
    ASSERT_EQ(Picker.Requests, 2u);
    EXPECT_EQ(Picker.SaveDirectory, Workspace + PLATFORM_OS_BACKSLASH + "\xe7\xb7\xa8\xe9\x9b\x86");
    EXPECT_EQ(Picker.SuggestedName, "\xe7\x85\xa7\xe6\x98\x8e.zescene");
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();

    App->OpenSceneDialog();
    ASSERT_EQ(Picker.OpenRequests, 1u);
    EXPECT_EQ(Picker.OpenDirectory, Workspace + PLATFORM_OS_BACKSLASH + "\xe5\xa0\xb4\xe9\x9d\xa2");
}

TEST_F(EditorSceneIOTest, InvalidSceneDirectoryDoesNotStartEitherNativePicker)
{
    Configuration.ScenePath.init(&Memory.MainArena, "\xff");
    App->SaveSceneAs();
    App->OpenSceneDialog();
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_FALSE(App->SceneOpenPending());
}

TEST_F(EditorSceneIOTest, NativeBoundaryResolvesDirectorySymlinksBeforeAcceptingPaths)
{
    std::error_code error = {};
    std::filesystem::create_directory_symlink(Temporary / "project-other", Temporary / "project" / "outside", error);
    if (error)
    {
        GTEST_SKIP() << "Creating test symlinks is unavailable: " << error.message();
    }
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, (Temporary / "project" / "outside" / "main.zescene").string()).Failed());
}

TEST_F(EditorSceneIOTest, SaveRequestIsBoundedAndDoesNotWriteOutsideTheExclusivePhase)
{
    Edit(4.f);
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestSceneSave(VFSPath::Parse("/another.zescene").Value(), App->Session.SceneEpoch()));
    EXPECT_FALSE(Exists(Path));
    EXPECT_FALSE(App->ProcessSceneSave(*Engine));
    ASSERT_TRUE(ProcessSave());
    EXPECT_TRUE(Exists(Path));
    EXPECT_FALSE(Owner.IsDirty());
    VFS.FailRename = true;
    EXPECT_TRUE(ProcessSave()); // the completed request is not replayed
}

TEST_F(EditorSceneIOTest, QueuedSaveCannotWriteIntoAReplacementSceneEpoch)
{
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&ECS));
    App->Session.EndMutationPhase();
    EXPECT_FALSE(ProcessSave());
    EXPECT_FALSE(Exists(Path));
    EXPECT_FALSE(App->RequestSceneSave(Path, App->Session.SceneEpoch() - 1));
}

TEST_F(EditorSceneIOTest, QueuedSaveCapturesTheCommittedResultOfEarlierPendingEdits)
{
    LocalTransformValue value = {};
    value.Position.x          = 19.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, value));
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessSave());
    SceneDocument loaded = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &loaded, &Diagnostics));
    Scene candidate = {};
    candidate.Initialize(&Memory.MainArena);
    ASSERT_TRUE(loaded.PopulateCandidate(candidate, Schemas, &Diagnostics));
    candidate.ForEach<TransformComponent>([](EntityID, const TransformComponent& transform) { EXPECT_FLOAT_EQ(transform.Position.x, 19.f); });
    candidate.Shutdown();
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, QueuedSaveRevalidatesDirectorySymlinksAtExecution)
{
    const auto path = VFSPath::Parse("/redirect/main.zescene").Value();
    ASSERT_TRUE(App->RequestSceneSave(path, App->Session.SceneEpoch()));
    std::error_code error = {};
    std::filesystem::create_directory_symlink(Temporary / "project-other", Temporary / "project" / "redirect", error);
    if (error)
    {
        GTEST_SKIP() << "Creating test symlinks is unavailable: " << error.message();
    }
    EXPECT_FALSE(ProcessSave());
    EXPECT_FALSE(Exists(path));
}

TEST_F(EditorSceneIOTest, SaveCommandReusesTheSuccessfulSourcePathWithoutClearingHistory)
{
    ASSERT_TRUE(Save(Path));
    Edit(7.f);
    App->SaveScene();
    ASSERT_TRUE(ProcessSave());
    EXPECT_EQ(Owner.SourcePath, Path);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, SaveAsPollsWithoutBlockingAndPublishesOnlyOneDialogAndOneSave)
{
    Edit(3.f);
    App->SaveScene(); // unnamed scene uses Save As
    App->SaveSceneAs();
    EXPECT_EQ(Picker.Requests, 1u);
    EXPECT_EQ(Picker.SuggestedName, "Main Scene.zescene");
    for (uint32_t update = 0; update < 10; ++update)
    {
        App->PollSceneSaveDialog();
    }
    EXPECT_FALSE(Exists(Path));
    Picker.Result.Complete((Temporary / "project" / "Scenes" / "main.zescene").string());
    App->PollSceneSaveDialog();
    EXPECT_FALSE(Exists(Path)); // native completion enqueues; it does not write
    ASSERT_TRUE(ProcessSave());
    EXPECT_EQ(Owner.SourcePath, Path);
    EXPECT_FALSE(Owner.IsDirty());
    App->PollSceneSaveDialog();
    EXPECT_TRUE(ProcessSave());
}

TEST_F(EditorSceneIOTest, SaveAsCancellationKeepsDirtyAndAllowsAnotherPicker)
{
    Edit(6.f);
    App->SaveSceneAs();
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(Owner.SourcePath.IsValid());
    App->SaveSceneAs();
    EXPECT_EQ(Picker.Requests, 2u);
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();
}

TEST_F(EditorSceneIOTest, DelayedSavePickerCannotSaveIntoANewSceneEpoch)
{
    App->SaveSceneAs();
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&ECS));
    App->Session.EndMutationPhase();
    Picker.Result.Complete((Temporary / "project" / "Scenes" / "main.zescene").string());
    App->PollSceneSaveDialog();
    EXPECT_TRUE(ProcessSave()); // no request survives the epoch mismatch
    EXPECT_FALSE(Exists(Path));
    EXPECT_FALSE(Owner.SourcePath.IsValid());
}

class EditorScenePickerOwnerTest : public EditorSceneIOTest, public testing::WithParamInterface<ScenePickerOwnerChange>
{
protected:
    void CheckLifecycleOwnerChange(bool close, bool consent)
    {
        Edit(3.f);
        if (close)
        {
            ASSERT_FALSE(App->OnCloseRequested());
        }
        else
        {
            ASSERT_TRUE(App->RequestNewScene());
        }
        const auto process = [&] { return close ? ProcessClose() : ProcessNewScene(); };
        uint64_t   serial  = 0;
        if (consent)
        {
            ASSERT_TRUE(process());
            serial = App->SceneChangePromptState().Serial;
            ASSERT_NE(serial, 0u);
            ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Discard, serial));
        }
        else
        {
            ASSERT_TRUE(App->Session.BeginMutationPhase());
            const auto target = App->Session.TargetFor(Entity);
            ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
            LocalTransformValue value = {};
            value.Position.x          = 9.f;
            ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
            App->Session.EndMutationPhase();
        }
        const auto  epoch                       = App->Session.SceneEpoch();
        const auto  revision                    = App->Session.DocumentRevision();
        const auto  undo                        = App->Session.History().UndoCount();
        const auto  uuid                        = Owner.UUID;

        EditorScene replacement                 = {};
        replacement.Session                     = &App->Session;
        replacement.UUID                        = Owner.UUID;
        replacement.Name                        = "Replacement";
        SceneSavePicker     other_window        = {};
        EditorConfiguration other_configuration = {};
        const auto          parent              = Temporary.string();
        other_configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
        switch (GetParam())
        {
            case ScenePickerOwnerChange::RenderOwner:
                App->CurrentScene = &replacement;
                break;
            case ScenePickerOwnerChange::SceneUUID:
                Owner.UUID = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
                break;
            case ScenePickerOwnerChange::Window:
                App->CurrentWindow = &other_window;
                break;
            case ScenePickerOwnerChange::Configuration:
                App->Configuration = &other_configuration;
                break;
            case ScenePickerOwnerChange::ConfiguredWorkspace:
                Configuration.WorkingSpacePath.init(&Memory.MainArena, parent.c_str());
                break;
            case ScenePickerOwnerChange::Workspace:
                App->WorkingSpacePath = parent.c_str();
                break;
            case ScenePickerOwnerChange::NullWorkspace:
                App->WorkingSpacePath = nullptr;
                break;
            case ScenePickerOwnerChange::EmptyWorkspace:
                App->WorkingSpacePath = "";
                break;
            case ScenePickerOwnerChange::ShorterWorkspace:
                App->WorkingSpacePath = "/";
                break;
        }
        EXPECT_FALSE(process());
        EXPECT_FALSE(Engine->CloseRequested.value.load());
        EXPECT_EQ(App->Session.SceneEpoch(), epoch);
        EXPECT_EQ(App->Session.DocumentRevision(), revision);
        EXPECT_EQ(App->Session.History().UndoCount(), undo);
        EXPECT_EQ(App->Session.Active(), !consent);
        EXPECT_TRUE(Owner.IsDirty());
        EXPECT_FALSE(App->SceneChangePromptState().Visible());
        EXPECT_FALSE(App->ChooseSceneChange(SceneChangeChoice::Discard, serial));
        const auto* transform = ECS.GetComponent<TransformComponent>(Entity);
        EXPECT_NE(transform, nullptr);
        if (transform)
        {
            EXPECT_FLOAT_EQ(transform->Position.x, consent ? 3.f : 9.f);
        }

        // Restore every borrowed fixture owner before assertions can return.
        // Neither a stale request nor a previously accepted discard may revive.
        App->CurrentScene     = &Owner;
        Owner.UUID            = uuid;
        App->CurrentWindow    = &Picker;
        App->Configuration    = &Configuration;
        App->WorkingSpacePath = Workspace.c_str();
        Configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
        ASSERT_TRUE(process());
        EXPECT_FALSE(Engine->CloseRequested.value.load());
        ASSERT_TRUE(ECS.IsAlive(Entity));
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        App->Session.Cancel();
        App->Session.EndMutationPhase();
        EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 3.f);
        if (close)
        {
            ASSERT_FALSE(App->OnCloseRequested());
        }
        else
        {
            ASSERT_TRUE(App->RequestNewScene());
        }
        ASSERT_TRUE(process());
        const auto fresh = App->SceneChangePromptState();
        EXPECT_TRUE(fresh.Visible());
        EXPECT_NE(fresh.Serial, serial);
        ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Cancel, fresh.Serial));
        ASSERT_TRUE(process());
        EXPECT_FALSE(Engine->CloseRequested.value.load());
        EXPECT_TRUE(ECS.IsAlive(Entity));
        EXPECT_EQ(App->Session.History().UndoCount(), undo);
    }

    void CheckPickerOwnerChange(bool save, bool queued = false)
    {
        Edit(3.f);
        const auto epoch    = App->Session.SceneEpoch();
        const auto revision = App->Session.DocumentRevision();
        const auto undo     = App->Session.History().UndoCount();
        const auto native   = (Temporary / "project" / "Scenes" / "main.zescene").string();
        if (queued)
        {
            ASSERT_TRUE(save ? App->RequestSceneSave(Path, epoch) : App->RequestSceneOpen(Path, epoch));
        }
        else if (save)
        {
            App->SaveSceneAs();
        }
        else
        {
            App->OpenSceneDialog();
        }
        ASSERT_EQ(save ? Picker.Requests : Picker.OpenRequests, queued ? 0u : 1u);

        // All replacements stay alive until after retirement. The current epoch
        // deliberately stays unchanged: epoch-only picker ownership is insufficient.
        EditorScene replacement                 = {};
        replacement.Session                     = &App->Session;
        replacement.UUID                        = Owner.UUID;
        replacement.Name                        = "Replacement";
        SceneSavePicker     other_window        = {};
        EditorConfiguration other_configuration = {};
        const auto          parent              = Temporary.string();
        other_configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
        const auto original_uuid = Owner.UUID;
        switch (GetParam())
        {
            case ScenePickerOwnerChange::RenderOwner:
                App->CurrentScene = &replacement;
                break;
            case ScenePickerOwnerChange::SceneUUID:
                Owner.UUID = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
                break;
            case ScenePickerOwnerChange::Window:
                App->CurrentWindow = &other_window;
                break;
            case ScenePickerOwnerChange::Configuration:
                App->Configuration = &other_configuration;
                break;
            case ScenePickerOwnerChange::ConfiguredWorkspace:
                Configuration.WorkingSpacePath.init(&Memory.MainArena, parent.c_str());
                break;
            case ScenePickerOwnerChange::Workspace:
                App->WorkingSpacePath = parent.c_str();
                break;
            case ScenePickerOwnerChange::NullWorkspace:
                App->WorkingSpacePath = nullptr;
                break;
            case ScenePickerOwnerChange::EmptyWorkspace:
                App->WorkingSpacePath = "";
                break;
            case ScenePickerOwnerChange::ShorterWorkspace:
                App->WorkingSpacePath = "/";
                break;
        }
        if (!queued)
        {
            ASSERT_TRUE((save ? Picker.Result : Picker.OpenResult).Complete(native));
        }
        if (save)
        {
            if (!queued)
            {
                App->PollSceneSaveDialog();
            }
            EXPECT_EQ(ProcessSave(), !queued); // stale completion never enqueues; queued work fails once
            EXPECT_FALSE(Exists(Path));
            EXPECT_FALSE(Exists(VFSPath::Parse("/project/Scenes/main.zescene").Value()));
            EXPECT_FALSE(Owner.SourcePath.IsValid());
            EXPECT_FALSE(replacement.SourcePath.IsValid());
        }
        else
        {
            if (queued)
            {
                EXPECT_FALSE(ProcessOpen());
            }
            else
            {
                App->PollSceneOpenDialog();
            }
            EXPECT_FALSE(App->SceneOpenPending());
        }
        EXPECT_EQ(App->Session.SceneEpoch(), epoch);
        EXPECT_EQ(App->Session.DocumentRevision(), revision);
        EXPECT_EQ(App->Session.History().UndoCount(), undo);
        EXPECT_TRUE(Owner.IsDirty());
        EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 3.f);

        // Restore fixture owners, then prove the retired callback neither blocks
        // a fresh picker nor replays after its original binding returns.
        App->CurrentScene     = &Owner;
        Owner.UUID            = original_uuid;
        App->CurrentWindow    = &Picker;
        App->Configuration    = &Configuration;
        App->WorkingSpacePath = Workspace.c_str();
        Configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
        if (save)
        {
            App->PollSceneSaveDialog();
            EXPECT_TRUE(ProcessSave());
            EXPECT_FALSE(Exists(Path));
            if (queued)
            {
                EXPECT_TRUE(App->RequestSceneSave(Path, epoch));
            }
            else
            {
                App->SaveSceneAs();
                EXPECT_EQ(Picker.Requests, 2u);
                EXPECT_TRUE(Picker.Result.Complete(native));
                App->PollSceneSaveDialog();
            }
            EXPECT_TRUE(ProcessSave());
            EXPECT_TRUE(Exists(Path));
            EXPECT_EQ(Owner.SourcePath, Path);
            EXPECT_FALSE(Owner.IsDirty());
        }
        else
        {
            App->PollSceneOpenDialog();
            EXPECT_FALSE(App->SceneOpenPending());
            if (queued)
            {
                EXPECT_TRUE(App->RequestSceneOpen(Path, epoch));
            }
            else
            {
                App->OpenSceneDialog();
                EXPECT_EQ(Picker.OpenRequests, 2u);
                EXPECT_TRUE(Picker.OpenResult.Complete(native));
                App->PollSceneOpenDialog();
            }
            EXPECT_TRUE(App->SceneOpenPending());
            App->OnClosing(); // no worker was dispatched; retire the fresh intent
        }
    }
};

TEST_P(EditorScenePickerOwnerTest, SaveAsRetiresASameEpochForeignOwnerBeforeEnqueuingOrCheckpointing)
{
    CheckPickerOwnerChange(true);
}

TEST_P(EditorScenePickerOwnerTest, OpenRetiresASameEpochForeignOwnerBeforeEnqueuingOrCancellingEdits)
{
    CheckPickerOwnerChange(false);
}

TEST_P(EditorScenePickerOwnerTest, QueuedSaveRejectsASameEpochForeignOwnerBeforeWritingOrCheckpointing)
{
    CheckPickerOwnerChange(true, true);
}

TEST_P(EditorScenePickerOwnerTest, QueuedOpenRejectsASameEpochForeignOwnerBeforeDispatchOrCancellingEdits)
{
    CheckPickerOwnerChange(false, true);
}

TEST_P(EditorScenePickerOwnerTest, NewSceneRetiresAForeignOwnerBeforeCancellingTheCurrentPreview)
{
    CheckLifecycleOwnerChange(false, false);
}

TEST_P(EditorScenePickerOwnerTest, CloseRetiresAForeignOwnerBeforeCancellingTheCurrentPreview)
{
    CheckLifecycleOwnerChange(true, false);
}

TEST_P(EditorScenePickerOwnerTest, NewSceneDiscardConsentNeverTransfersToAForeignOwner)
{
    CheckLifecycleOwnerChange(false, true);
}

TEST_P(EditorScenePickerOwnerTest, CloseDiscardConsentNeverTransfersToAForeignOwner)
{
    CheckLifecycleOwnerChange(true, true);
}

INSTANTIATE_TEST_SUITE_P(CapturedOwners, EditorScenePickerOwnerTest, testing::Values(ScenePickerOwnerChange::RenderOwner, ScenePickerOwnerChange::SceneUUID, ScenePickerOwnerChange::Window, ScenePickerOwnerChange::Configuration, ScenePickerOwnerChange::ConfiguredWorkspace, ScenePickerOwnerChange::Workspace, ScenePickerOwnerChange::NullWorkspace, ScenePickerOwnerChange::EmptyWorkspace, ScenePickerOwnerChange::ShorterWorkspace), [](const testing::TestParamInfo<ScenePickerOwnerChange>& info) {
    switch (info.param)
    {
        case ScenePickerOwnerChange::RenderOwner:
            return "RenderOwner";
        case ScenePickerOwnerChange::SceneUUID:
            return "SceneUUID";
        case ScenePickerOwnerChange::Window:
            return "Window";
        case ScenePickerOwnerChange::Configuration:
            return "Configuration";
        case ScenePickerOwnerChange::ConfiguredWorkspace:
            return "ConfiguredWorkspace";
        case ScenePickerOwnerChange::Workspace:
            return "Workspace";
        case ScenePickerOwnerChange::NullWorkspace:
            return "NullWorkspace";
        case ScenePickerOwnerChange::EmptyWorkspace:
            return "EmptyWorkspace";
        case ScenePickerOwnerChange::ShorterWorkspace:
            return "ShorterWorkspace";
    }
    return "Unknown";
});

TEST_F(EditorSceneIOTest, SceneFilePickersAcceptEqualWorkspaceValuesAndReuseIdlePollingStorage)
{
    LoadSlots();
    Edit(3.f);
    const auto        epoch     = App->Session.SceneEpoch();
    const auto        native    = (Temporary / "project" / "Scenes" / "main.zescene").string();
    const std::string relocated = Workspace;
    App->SaveSceneAs();
    ASSERT_EQ(Picker.Requests, 1u);
    App->WorkingSpacePath = relocated.c_str();
    Configuration.WorkingSpacePath.init(&Memory.MainArena, Workspace.c_str());
    const auto offset = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        App->PollSceneSaveDialog();
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(Exists(Path));
    ASSERT_TRUE(Picker.Result.Complete(native));
    App->PollSceneSaveDialog();
    ASSERT_TRUE(ProcessSave());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(Owner.IsDirty());

    App->OpenSceneDialog();
    ASSERT_EQ(Picker.OpenRequests, 1u);
    App->WorkingSpacePath  = Workspace.c_str();
    const auto open_offset = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        App->PollSceneOpenDialog();
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, open_offset);
    EXPECT_FALSE(App->SceneOpenPending());
    ASSERT_TRUE(Picker.OpenResult.Complete(native));
    App->PollSceneOpenDialog();
    ASSERT_TRUE(App->SceneOpenPending());
    FinishOpen();
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_NE(App->Session.SceneEpoch(), epoch);
    EXPECT_NE(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(static_cast<EditorScene*>(App->CurrentScene)->IsDirty());
    uint32_t count = 0;
    Engine->Scene->ForEach<TransformComponent>([&](EntityID, const TransformComponent& value) {
        ++count;
        EXPECT_FLOAT_EQ(value.Position.x, 3.f);
    });
    EXPECT_EQ(count, 1u);
}

TEST_F(EditorSceneIOTest, SceneFileEntryPointsRejectForeignThreadsWithoutPublishingOrConsumingResults)
{
    Edit(4.f);
    const auto  epoch         = App->Session.SceneEpoch();
    const auto  native        = (Temporary / "project" / "Scenes" / "main.zescene").string();
    bool        save_accepted = true, open_accepted = true;
    std::thread requester([&] {
        App->SaveScene();
        App->SaveSceneAs();
        App->OpenSceneDialog();
        App->OpenScene(native.c_str());
        save_accepted = App->RequestSceneSave(Path, epoch);
        open_accepted = App->RequestSceneOpen(Path, epoch);
    });
    requester.join();
    EXPECT_FALSE(save_accepted);
    EXPECT_FALSE(open_accepted);
    EXPECT_EQ(Picker.Requests, 0u);
    EXPECT_EQ(Picker.OpenRequests, 0u);
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_TRUE(ProcessSave());
    EXPECT_FALSE(Exists(Path));

    App->SaveSceneAs();
    ASSERT_EQ(Picker.Requests, 1u);
    ASSERT_TRUE(Picker.Result.Complete(native));
    std::thread save_poller([&] { App->PollSceneSaveDialog(); });
    save_poller.join();
    EXPECT_TRUE(ProcessSave());
    EXPECT_FALSE(Exists(Path));
    App->PollSceneSaveDialog();
    EXPECT_TRUE(ProcessSave());
    EXPECT_TRUE(Exists(Path));
    EXPECT_FALSE(Owner.IsDirty());

    App->OpenSceneDialog();
    ASSERT_EQ(Picker.OpenRequests, 1u);
    ASSERT_TRUE(Picker.OpenResult.Complete(native));
    std::thread open_poller([&] { App->PollSceneOpenDialog(); });
    open_poller.join();
    EXPECT_FALSE(App->SceneOpenPending());
    App->PollSceneOpenDialog();
    EXPECT_TRUE(App->SceneOpenPending());
    App->OnClosing();
}

TEST_F(EditorSceneIOTest, SceneFileOwnerCaptureRejectsInconsistentAndOverlongWorkspacesWithoutQueuing)
{
    const auto        epoch = App->Session.SceneEpoch();
    const std::string oversized(VFS_MAX_PATH, 'x');
    for (const auto* invalid : {"", "/", oversized.c_str()})
    {
        App->WorkingSpacePath = invalid;
        App->SaveSceneAs();
        App->OpenSceneDialog();
        EXPECT_FALSE(App->RequestSceneSave(Path, epoch));
        EXPECT_FALSE(App->RequestSceneOpen(Path, epoch));
        EXPECT_EQ(Picker.Requests, 0u);
        EXPECT_EQ(Picker.OpenRequests, 0u);
        EXPECT_FALSE(App->SceneOpenPending());
        EXPECT_TRUE(ProcessSave());
    }
    App->WorkingSpacePath = Workspace.c_str();
    EXPECT_FALSE(Exists(Path));
}

TEST_F(EditorSceneIOTest, SceneLifecycleEntryPointsRejectForeignThreadsWithoutQueuingConsentOrShutdown)
{
    Edit(3.f);
    bool        accepted = true;
    std::thread requester([&] {
        accepted = App->RequestNewScene();
        EXPECT_FALSE(App->OnCloseRequested());
    });
    requester.join();
    EXPECT_FALSE(accepted);
    EXPECT_TRUE(ProcessNewScene());
    EXPECT_TRUE(ProcessClose());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneIOTest, SceneLifecycleConsentAcceptsEqualWorkspaceValuesAndReusesPendingStorage)
{
    Edit(3.f);
    const std::string relocated = Workspace;
    for (const bool close : {false, true})
    {
        if (close)
        {
            ASSERT_FALSE(App->OnCloseRequested());
        }
        else
        {
            ASSERT_TRUE(App->RequestNewScene());
        }
        App->WorkingSpacePath = relocated.c_str();
        const auto offset     = Memory.MainArena.m_current_offset;
        uint64_t   serial     = 0;
        for (uint32_t i = 0; i < 1000; ++i)
        {
            EXPECT_TRUE(close ? ProcessClose() : ProcessNewScene());
            const auto prompt = App->SceneChangePromptState();
            EXPECT_TRUE(prompt.Visible());
            if (!i)
            {
                serial = prompt.Serial;
            }
            EXPECT_EQ(prompt.Serial, serial);
        }
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
        App->WorkingSpacePath = Workspace.c_str();
        ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Cancel, serial));
        ASSERT_TRUE(close ? ProcessClose() : ProcessNewScene());
        EXPECT_FALSE(Engine->CloseRequested.value.load());
        EXPECT_EQ(App->Session.History().UndoCount(), 1u);
        EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 3.f);
    }
}

TEST_F(EditorSceneIOTest, QueuedOpenOwnerMismatchRetiresBeforeCancellingTheCurrentPreview)
{
    Edit(3.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
    App->Session.EndMutationPhase();
    const auto uuid = Owner.UUID;
    Owner.UUID      = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    EXPECT_FALSE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_TRUE(App->Session.Active());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    Owner.UUID = uuid;
    EXPECT_TRUE(ProcessOpen()); // restoration never replays the retired intent
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    App->Session.Cancel();
    App->Session.EndMutationPhase();
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 3.f);
}

TEST_F(EditorSceneIOTest, SavePickerFailureAndOutsideWorkspaceResultsDoNotWrite)
{
    Edit(2.f);
    App->SaveSceneAs();
    EXPECT_TRUE(Picker.Result.Fail());
    App->PollSceneSaveDialog();
    App->SaveSceneAs();
    Picker.Result.Complete((Temporary / "project-other" / "main.zescene").string());
    App->PollSceneSaveDialog();
    EXPECT_TRUE(ProcessSave());
    EXPECT_FALSE(Exists(Path));
    EXPECT_TRUE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, NewSceneClearsPureEntitiesAndRuntimeCommandsAndChangesIdentityAndEpoch)
{
    WorldCommands commands = {};
    commands.Initialize(&Memory.MainArena);
    commands.SpawnEntity();
    Engine->WorldCommands     = &commands;
    const auto previous_uuid  = Owner.UUID;
    const auto previous_epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    EXPECT_FALSE(App->RequestNewScene());
    EXPECT_FALSE(App->ProcessNewScene(*Engine)); // no mutation outside the phase
    EXPECT_EQ(ECS.AliveCount(), 1u);
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_TRUE(commands.IsEmpty());
    EXPECT_NE(Owner.UUID, previous_uuid);
    EXPECT_FALSE(Owner.UUID.is_nil());
    EXPECT_GT(App->Session.SceneEpoch(), previous_epoch);
    EXPECT_TRUE(Owner.IsDirty()); // a new, unsaved document
    EXPECT_STREQ(Owner.Name, "Untitled Scene");
    EXPECT_FALSE(Owner.SourcePath.IsValid());
    EXPECT_TRUE(ProcessNewScene()); // no replay
    Engine->WorldCommands = nullptr;
}

TEST_F(EditorSceneIOTest, NewScenePreservesDirtyContentAndRevalidatesEarlierQueuedEdits)
{
    Edit(5.f);
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Cancel, App->SceneChangePromptState().Serial));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 5.f);
    ASSERT_TRUE(Save(Path));
    ASSERT_TRUE(App->RequestNewScene());
    LocalTransformValue value = {};
    value.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, value));
    EXPECT_TRUE(ProcessNewScene());
    EXPECT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_EQ(ECS.AliveCount(), 1u);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(Owner.SourcePath, Path);
}

TEST_F(EditorSceneIOTest, NewSceneRestoresPreviewBeforeActorDestructionAndClearsBothEntityTiers)
{
    struct InspectDestroy : Actor
    {
        bool* Restored = nullptr;
        void  OnDestroy() override
        {
            const auto* transform = GetComponent<TransformComponent>();
            if (Restored)
            {
                *Restored = transform && transform->Position.x == 0.f && transform->WorldTransform(0, 3) == 0.f;
            }
        }
    };
    ActorManager actors = {};
    actors.Initialize(&Memory.MainArena, ECS);
    Engine->ActorManager    = &actors;
    const auto actor_handle = actors.Create<InspectDestroy>();
    auto*      actor        = static_cast<InspectDestroy*>(actors.Access(actor_handle));
    bool       restored     = false;
    actor->Restored         = &restored;
    actor->AddComponent<UUIDComponent>({*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
    actor->AddComponent<TransformComponent>({});
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(actor->GetEntityID());
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 8.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
    App->Session.EndMutationPhase();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_TRUE(restored);
    EXPECT_EQ(actors.Count(), 0u);
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FALSE(actors.IsLive(actor_handle));
    actors.Shutdown();
    Engine->ActorManager = nullptr;
}

TEST_F(EditorSceneIOTest, NewSceneRetiresQueuedSavesAndDelayedPickerResults)
{
    App->SaveSceneAs();
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_TRUE(ProcessSave());
    Picker.Result.Complete((Temporary / "project" / "Scenes" / "main.zescene").string());
    App->PollSceneSaveDialog();
    EXPECT_TRUE(ProcessSave());
    EXPECT_FALSE(Exists(Path));
    EXPECT_FALSE(Owner.SourcePath.IsValid());
}

TEST_F(EditorSceneIOTest, NewSceneRejectsAStaleQueuedLifecycleRequest)
{
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&ECS));
    App->Session.EndMutationPhase();
    EXPECT_FALSE(ProcessNewScene());
    EXPECT_EQ(ECS.AliveCount(), 1u);
}

TEST_F(EditorSceneIOTest, NativeBoundaryCannotNormalizeALiteralBackslashIntoADifferentPosixFile)
{
#if defined(_WIN32)
    GTEST_SKIP() << "Backslash is a native separator on Windows";
#else
    EXPECT_TRUE(SceneSourcePathFromNative(Workspace, (Temporary / "project" / "a\\b.zescene").string()).Failed());
#endif
}

TEST_F(EditorSceneIOTest, NativeBoundaryUsesActualFilesystemCaseSensitivityInsteadOfByteFolding)
{
    std::error_code error          = {};
    const auto      different_case = Temporary / "PROJECT";
    const bool      same_directory = std::filesystem::equivalent(Workspace, different_case, error);
    const auto      result         = SceneSourcePathFromNative(Workspace, (different_case / "Scenes" / "main.zescene").string());
    if (same_directory && !error)
    {
        ASSERT_TRUE(result.Succeeded());
        EXPECT_EQ(result.Value(), Path);
    }
    else
    {
        EXPECT_TRUE(result.Failed());
    }
}

TEST_F(EditorSceneIOTest, SceneNameAdapterBoundsNonTerminatedInputBeforeAllocatingASnapshot)
{
    std::array<char, SceneDocument::MaxNameBytes + 1> name = {};
    name.fill('x');
    Owner.Name = name.data();
    EXPECT_FALSE(Save(Path));
    EXPECT_FALSE(Exists(Path));
    std::string_view result = "unchanged";
    EXPECT_FALSE(TrySceneSourceName(name.data(), &result));
    EXPECT_EQ(result, "unchanged");
    name.back() = '\0';
    ASSERT_TRUE(TrySceneSourceName(name.data(), &result));
    EXPECT_EQ(result.size(), SceneDocument::MaxNameBytes);
    ASSERT_TRUE(Save(Path));
    Owner.Name = "Main Scene";
}

TEST_F(EditorSceneIOTest, NewSceneCancelsPreviewButPreservesPreviouslyCommittedUnsavedContent)
{
    Edit(4.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 10.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
    App->Session.EndMutationPhase();
    ASSERT_TRUE(App->RequestNewScene());
    EXPECT_TRUE(ProcessNewScene());
    EXPECT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 4.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorSceneIOTest, QueuedSaveWritesARealProjectFileReadableThroughAnIndependentVFS)
{
    const auto                         caps    = VFSBackendCaps::Read | VFSBackendCaps::Write | VFSBackendCaps::List;
    ZEngine::Core::VFS::VFSDiskBackend backend = {};
    backend.Initialize(Workspace.c_str(), caps, &Memory.MainArena);
    VFSContext writer = {};
    writer.Initialize(&Memory.MainArena);
    ASSERT_TRUE(writer.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    Engine->VFS = &writer;
    Edit(13.f);
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessSave());
    EXPECT_TRUE(std::filesystem::is_regular_file(Temporary / "project" / "Scenes" / "main.zescene"));
    VFSContext reader = {};
    reader.Initialize(&Memory.MainArena);
    ASSERT_TRUE(reader.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    SceneDocument loaded = {};
    ASSERT_TRUE(SceneDocument::Read(reader, Path, Schemas, &loaded, &Diagnostics));
    EXPECT_EQ(loaded.EntityCount(), 1u);
    ASSERT_NE(loaded.Settings(), nullptr);
    EXPECT_EQ(loaded.Settings()->UUID, Owner.UUID);
    EXPECT_EQ(loaded.Settings()->Name, "Main Scene");
    reader.Shutdown();
    writer.Shutdown();
    Engine->VFS = &VFS;
}

TEST_F(EditorSceneIOTest, CandidatePublicationRequiresTheOriginalEpochAndExclusivePhase)
{
    LoadSlots();
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    const auto epoch = App->Session.SceneEpoch();
    EXPECT_FALSE(App->PublishSceneCandidate(*Engine, candidate, epoch));
    EXPECT_FALSE(Publish(candidate, epoch + 1));
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_EQ(App->CurrentScene, &Owner);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    ASSERT_TRUE(Publish(candidate, epoch));
    EXPECT_EQ(candidate, nullptr);
    EXPECT_NE(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.Scene(), Engine->Scene);
    EXPECT_NE(Engine->ActorManager, nullptr);
    EXPECT_EQ(Engine->Scene->AliveCount(), 1u);
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    EXPECT_STREQ(static_cast<const EditorScene*>(App->CurrentScene)->Name, "Replacement scene");
    EXPECT_FALSE(static_cast<EditorScene*>(App->CurrentScene)->IsDirty());
    EXPECT_EQ(Owner.Session, nullptr);
}

TEST_F(EditorSceneIOTest, DirtyPublicationCancelsPreviewButKeepsCommittedSceneAndCandidate)
{
    LoadSlots();
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    Edit(3.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 8.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    const auto epoch = App->Session.SceneEpoch();
    EXPECT_FALSE(Publish(candidate, epoch));
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 3.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Engine->Scene, &ECS);
    ASSERT_NE(candidate, nullptr);
}

TEST_F(EditorSceneIOTest, PublicationRestoresOriginalWorldBeforeActorDestructionAndRetiresCommands)
{
    struct ObserveDestroy : Actor
    {
        ZEngine::EngineContext* Engine   = nullptr;
        Scene*                  Original = nullptr;
        bool*                   Observed = nullptr;
        void                    OnDestroy() override
        {
            const auto* transform = GetComponent<TransformComponent>();
            *Observed             = Engine->Scene == Original && transform && transform->Position.x == 0.f && transform->WorldTransform(0, 3) == 0.f;
            if (Engine->WorldCommands)
            {
                Engine->WorldCommands->SpawnEntity();
            }
        }
    };
    LoadSlots();
    ActorManager actors = {};
    actors.Initialize(&Memory.MainArena, ECS);
    Engine->ActorManager                 = &actors;
    ZEngine::ECS::WorldCommands commands = {};
    commands.Initialize(&Memory.MainArena);
    commands.SpawnEntity();
    Engine->WorldCommands = &commands;
    const auto handle     = actors.Create<ObserveDestroy>();
    auto*      actor      = static_cast<ObserveDestroy*>(actors.Access(handle));
    bool       observed   = false;
    actor->Engine         = Engine.get();
    actor->Original       = &ECS;
    actor->Observed       = &observed;
    actor->AddComponent<UUIDComponent>({*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
    actor->AddComponent<TransformComponent>({});
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(actor->GetEntityID());
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    EXPECT_TRUE(Publish(candidate, App->Session.SceneEpoch()));
    EXPECT_TRUE(observed);
    EXPECT_EQ(actors.Count(), 0u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_TRUE(commands.IsEmpty());
    App->OnClosing();
    App->OnWorldShutdown(*Engine);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(Engine->ActorManager, &actors);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
    actors.Shutdown();
    Engine->ActorManager  = nullptr;
    Engine->WorldCommands = nullptr;
}

TEST_F(EditorSceneIOTest, RepeatedPublicationReclaimsBothSlotsWithoutGrowingPersistentStorage)
{
    LoadSlots();
    const auto baseline = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 100; ++i)
    {
        std::unique_ptr<EditorSceneCandidate> candidate = {};
        ASSERT_TRUE(Prepare(&candidate));
        ASSERT_TRUE(Publish(candidate, App->Session.SceneEpoch()));
        EXPECT_EQ(Engine->Scene->AliveCount(), 1u);
        EXPECT_NE(Engine->EditorSceneLoadArenaAInUse.value.load(), Engine->EditorSceneLoadArenaBInUse.value.load());
        EXPECT_EQ(Memory.MainArena.m_current_offset, baseline);
    }
    App->OnClosing();
    App->OnWorldShutdown(*Engine);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(Engine->ActorManager, nullptr);
    EXPECT_EQ(App->CurrentScene, nullptr);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneIOTest, NewSceneCanResetALoadedWorldWithoutReleasingItsActiveSlot)
{
    LoadSlots();
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    ASSERT_TRUE(Publish(candidate, App->Session.SceneEpoch()));
    auto*      loaded_world = Engine->Scene;
    const auto epoch        = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(Engine->Scene, loaded_world);
    EXPECT_EQ(loaded_world->AliveCount(), 0u);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    const auto* owner = static_cast<const EditorScene*>(App->CurrentScene);
    EXPECT_STREQ(owner->Name, "Untitled Scene");
    EXPECT_FALSE(owner->SourcePath.IsValid());
    EXPECT_FALSE(owner->SourceReadOnly);
    EXPECT_EQ(owner->Session, &App->Session);
}

TEST_F(EditorSceneIOTest, OpenIsQueuedAndOnlyASuccessfulLoadUpdatesRecentScenes)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    Tetragrama::Components::Recent() = {};
    const auto epoch                 = App->Session.SceneEpoch();
    const auto native                = (Temporary / "project" / "Scenes" / "main.zescene").string();
    App->OpenScene(native.c_str());
    EXPECT_TRUE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(Tetragrama::Components::Recent().Count, 0);
    EXPECT_FALSE(App->ProcessSceneOpen(*Engine));
    EXPECT_TRUE(ProcessOpen());
    FinishOpen();
    ASSERT_NE(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.Scene(), Engine->Scene);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Engine->ActorManager->Count(), 0u);
    EXPECT_EQ(Engine->Scene->AliveCount(), 1u);
    EXPECT_EQ(static_cast<const EditorScene*>(App->CurrentScene)->SourcePath, Path);
    ASSERT_EQ(Tetragrama::Components::Recent().Count, 1);
    EXPECT_STREQ(Tetragrama::Components::Recent().Items[0].Path, native.c_str());
}

TEST_F(EditorSceneIOTest, MissingOpenSourceAndDirtyCurrentScenePreserveTheActiveWorld)
{
    LoadSlots();
    Tetragrama::Components::Recent() = {};
    const auto epoch                 = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    EXPECT_FALSE(App->RequestSceneOpen(Path, epoch));
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Tetragrama::Components::Recent().Count, 0);
    ASSERT_TRUE(Save(Path));
    Edit(5.f);
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    EXPECT_TRUE(ProcessOpen());
    EXPECT_TRUE(App->SceneOpenPending());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    ASSERT_TRUE(App->ChooseSceneChange(SceneChangeChoice::Cancel, App->SceneChangePromptState().Serial));
    EXPECT_TRUE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 5.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(Engine->Scene, &ECS);
}

TEST_F(EditorSceneIOTest, EditWhileWorkerParsesPreventsReplacementOfUnsavedContent)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    ASSERT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessOpen());
    Edit(7.f);
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 7.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneIOTest, NewSceneRejectsAlreadyDispatchedOpenCompletionByEpoch)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    ASSERT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessOpen());
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto epoch = App->Session.SceneEpoch();
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_STREQ(static_cast<const EditorScene*>(App->CurrentScene)->Name, "Untitled Scene");
}

TEST_F(EditorSceneIOTest, DelayedOpenPickerDoesNotApplyToANewEpochAndCanBeReused)
{
    const auto native = (Temporary / "project" / "Scenes" / "main.zescene").string();
    App->OpenSceneDialog();
    App->OpenSceneDialog();
    EXPECT_EQ(Picker.OpenRequests, 1u);
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    Picker.OpenResult.Complete(native);
    App->PollSceneOpenDialog();
    EXPECT_FALSE(App->SceneOpenPending());
    App->OpenSceneDialog();
    EXPECT_EQ(Picker.OpenRequests, 2u);
    Picker.OpenResult.Complete({});
    App->PollSceneOpenDialog();
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
}

TEST_F(EditorSceneIOTest, ClosingRetiresDelayedPickerAndRejectsNewLifecycleRequests)
{
    App->OpenSceneDialog();
    App->OnClosing();
    Picker.OpenResult.Complete((Temporary / "project" / "Scenes" / "main.zescene").string());
    App->PollSceneOpenDialog();
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_FALSE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    App->OpenSceneDialog();
    EXPECT_EQ(Picker.OpenRequests, 1u);
}

TEST_F(EditorSceneIOTest, ShutdownWaitsForOpenWorkBeforeReleasingItsContextOrVFS)
{
    struct BlockingReadVFS : VFSContext
    {
        std::atomic<bool>    Started = false;
        std::atomic<bool>    Release = false;
        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            Started.store(true, std::memory_order_release);
            while (!Release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            return VFSContext::Open(path, flags);
        }
    } blocked;
    ASSERT_TRUE(Save(Path));
    blocked.Initialize(&Memory.MainArena);
    ASSERT_TRUE(blocked.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
    Engine->VFS = &blocked;
    ASSERT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessOpen());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blocked.Started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    EXPECT_TRUE(blocked.Started.load(std::memory_order_acquire));
    App->OnClosing();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    blocked.Release.store(true, std::memory_order_release);
    ZEngine::Helpers::ThreadPoolHelper::Shutdown();
    App->OnWorldShutdown(*Engine);
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->CurrentScene, &Owner);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    blocked.Shutdown();
    Engine->VFS = &VFS;
    ZEngine::Helpers::ThreadPoolHelper::Initialize();
}

TEST_F(EditorSceneIOTest, OpenWithoutAWorkerDoesNotFallbackToBlockingMainThreadIO)
{
    ASSERT_TRUE(Save(Path));
    ZEngine::Helpers::ThreadPoolHelper::Shutdown();
    EXPECT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    ZEngine::Helpers::ThreadPoolHelper::Initialize();
}

class EditorSceneOpenOwnerTest : public EditorSceneIOTest, public testing::WithParamInterface<SceneOpenOwnerChange>
{
};

TEST_P(EditorSceneOpenOwnerTest, OwnerChangeWhileReadingRetiresWorkWithoutTouchingTheCurrentPreview)
{
    CheckStaleOpenOwner(GetParam(), false);
}

TEST_P(EditorSceneOpenOwnerTest, OwnerChangeAfterReadingRejectsTheReadyCompletionBeforePublication)
{
    CheckStaleOpenOwner(GetParam(), true);
}

INSTANTIATE_TEST_SUITE_P(CapturedOwners, EditorSceneOpenOwnerTest, testing::Values(SceneOpenOwnerChange::VFS, SceneOpenOwnerChange::Schemas, SceneOpenOwnerChange::Workspace, SceneOpenOwnerChange::EmptyWorkspace, SceneOpenOwnerChange::NullWorkspace, SceneOpenOwnerChange::RenderOwner, SceneOpenOwnerChange::SceneUUID, SceneOpenOwnerChange::World, SceneOpenOwnerChange::Actors, SceneOpenOwnerChange::Engine), [](const testing::TestParamInfo<SceneOpenOwnerChange>& info) {
    switch (info.param)
    {
        case SceneOpenOwnerChange::VFS:
            return "VFS";
        case SceneOpenOwnerChange::Schemas:
            return "Schemas";
        case SceneOpenOwnerChange::Workspace:
            return "Workspace";
        case SceneOpenOwnerChange::EmptyWorkspace:
            return "EmptyWorkspace";
        case SceneOpenOwnerChange::NullWorkspace:
            return "NullWorkspace";
        case SceneOpenOwnerChange::RenderOwner:
            return "RenderOwner";
        case SceneOpenOwnerChange::SceneUUID:
            return "SceneUUID";
        case SceneOpenOwnerChange::World:
            return "World";
        case SceneOpenOwnerChange::Actors:
            return "Actors";
        case SceneOpenOwnerChange::Engine:
            return "Engine";
    }
    return "Unknown";
});

TEST_F(EditorSceneIOTest, OpenComparesTheOwnedWorkspaceValueRatherThanItsCStringAddress)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(StartBlockedOpen());
    const std::string workspace_copy = Workspace;
    App->WorkingSpacePath            = workspace_copy.c_str();
    ReadGate.Release.store(true, std::memory_order_release);
    FinishOpen();
    App->WorkingSpacePath = Workspace.c_str();
    EXPECT_NE(Engine->Scene, &ECS);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.Scene(), Engine->Scene);
    EXPECT_EQ(Tetragrama::Components::Recent().Count, 1);
}

TEST_F(EditorSceneIOTest, SceneOpenIntentFromANonOwnerThreadIsRejectedWithoutAccessingMutableState)
{
    const auto  epoch    = App->Session.SceneEpoch();
    bool        accepted = true;
    std::thread worker([&] { accepted = App->RequestSceneOpen(Path, epoch); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
}

TEST_F(EditorSceneIOTest, LoadedActorShutdownObservesItsOriginalWorldAndRestoredPreview)
{
    struct ObserveDestroy : Actor
    {
        ZEngine::EngineContext* Engine   = nullptr;
        Scene*                  Original = nullptr;
        bool*                   Observed = nullptr;
        void                    OnDestroy() override
        {
            const auto* transform = GetComponent<TransformComponent>();
            *Observed             = Engine->Scene == Original && transform && transform->Position.x == 0.f && transform->WorldTransform(0, 3) == 0.f;
        }
    };
    LoadSlots();
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    ASSERT_TRUE(Publish(candidate, App->Session.SceneEpoch()));
    const auto handle = Engine->ActorManager->Create<ObserveDestroy>();
    auto*      actor  = static_cast<ObserveDestroy*>(Engine->ActorManager->Access(handle));
    ASSERT_NE(actor, nullptr);
    bool observed   = false;
    actor->Engine   = Engine.get();
    actor->Original = Engine->Scene;
    actor->Observed = &observed;
    actor->AddComponent<UUIDComponent>({*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
    actor->AddComponent<TransformComponent>({});
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(actor->GetEntityID());
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    App->OnClosing();
    App->OnWorldShutdown(*Engine);
    EXPECT_TRUE(observed);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(Engine->ActorManager, nullptr);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneIOTest, MissingMeshBindingRejectsOpenWithoutClearingTheOriginalWorld)
{
    LoadSlots();
    Tetragrama::Components::Recent() = {};
    ECS.AddComponent<MeshComponent>(Entity, {*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333"), 12345});
    ASSERT_TRUE(Save(Path));
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->CurrentScene, &Owner);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_EQ(ECS.GetComponent<MeshComponent>(Entity)->RenderInstanceId, 12345u);
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_EQ(Tetragrama::Components::Recent().Count, 0);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneIOTest, UncachedSceneMeshesLoadOnceOnAWorkerBeforePublishingAllPlacedInstances)
{
    PrepareUncachedMesh();
    LoadSlots();
    const auto mesh_uuid = ECS.GetComponent<MeshComponent>(Entity)->MeshUUID;
    const auto second    = ECS.CreateEntity();
    ECS.AddComponent<UUIDComponent>(second, {*uuids::uuid::from_string("55555555-5555-4555-8555-555555555555")});
    ECS.AddComponent<TransformComponent>(second, {});
    ECS.AddComponent<MeshComponent>(second, {mesh_uuid, 54321});
    ASSERT_TRUE(Save(Path));
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(StartBlockedOpen());
    auto* assets = ZEngine::Managers::AssetManager::Instance();
    EXPECT_EQ(assets->Meshes.size(), 0u);
    ASSERT_TRUE(CompleteBlockedRead());
    // Worker completion fills a reusable CPU cache, never the active world.
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ECS.GetComponent<MeshComponent>(Entity)->RenderInstanceId, 12345u);
    EXPECT_EQ(assets->Meshes.size(), 1u);
    EXPECT_EQ(assets->NodeHierarchies.size(), 1u);
    EXPECT_EQ(ReadGate.MeshReads.load(), 1u);
    ASSERT_TRUE(ProcessOpen());
    ASSERT_NE(Engine->Scene, &ECS);
    auto* owner = static_cast<EditorScene*>(App->CurrentScene);
    EXPECT_EQ(owner->Instances.size(), 2u);
    EXPECT_EQ(Engine->Scene->AliveCount(), 2u);
    EXPECT_EQ(Engine->ActorManager->Count(), 0u);
    EXPECT_GT(App->Session.SceneEpoch(), epoch);
    EXPECT_FALSE(owner->IsDirty());
    for (const auto& instance : owner->Instances)
    {
        EXPECT_EQ(instance.MeshUUID, mesh_uuid);
    }
}

TEST_F(EditorSceneIOTest, CancelWhileReadingAnUncachedMeshPublishesNeitherTheMeshNorTheScene)
{
    PrepareUncachedMesh();
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    const auto epoch  = App->Session.SceneEpoch();
    ReadGate.OnlyMesh = true;
    ASSERT_TRUE(StartBlockedOpen());
    EXPECT_EQ(ReadGate.MeshReads.load(), 1u);
    Engine->VFS = &VFS;
    EXPECT_FALSE(ProcessOpen());
    EXPECT_TRUE(App->SceneOpenPending());
    ReadGate.Release.store(true, std::memory_order_release);
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ZEngine::Managers::AssetManager::Instance()->Meshes.size(), 0u);
    EXPECT_EQ(ZEngine::Managers::AssetManager::Instance()->MeshCacheAllocations.size(), 0u);
    EXPECT_EQ(ECS.GetComponent<MeshComponent>(Entity)->RenderInstanceId, 12345u);
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, UnreadableUncachedMeshKeepsTheSavedWorldAndReleasesLoadStorage)
{
    PrepareUncachedMesh();
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    ASSERT_TRUE(VFS.Remove(VFSPath::Parse("/Meshes/triangle.zemesh").Value()).Succeeded());
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ZEngine::Managers::AssetManager::Instance()->Meshes.size(), 0u);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, ChangedAssetRegistryCancelsOpenEvenWhenVFSAndSceneEpochAreUnchanged)
{
    PrepareUncachedMesh();
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(StartBlockedOpen());
    auto* assets     = ZEngine::Managers::AssetManager::Instance();
    auto* original   = assets->Registry;
    assets->Registry = nullptr;
    EXPECT_FALSE(ProcessOpen());
    assets->Registry = original;
    ReadGate.Release.store(true, std::memory_order_release);
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(assets->Meshes.size(), 0u);
    EXPECT_EQ(ReadGate.MeshReads.load(), 0u);
}

TEST_F(EditorSceneIOTest, QueuedOpenRevalidatesTheProjectBoundaryBeforeReading)
{
    ASSERT_TRUE(Save(Path));
    ASSERT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    std::filesystem::rename(Temporary / "project" / "Scenes", Temporary / "project" / "OriginalScenes");
    std::error_code error = {};
    std::filesystem::create_directory_symlink(Temporary / "project-other", Temporary / "project" / "Scenes", error);
    if (error)
    {
        GTEST_SKIP() << "Directory symlink creation unavailable: " << error.message();
    }
    EXPECT_FALSE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneIOTest, NewSceneDiscardRequiresExplicitCurrentRevisionConsent)
{
    Edit(5.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ECS.AliveCount(), 1u);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    EXPECT_EQ(ECS.AliveCount(), 1u); // UI response itself is not a mutation
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_NE(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_TRUE(Owner.IsDirty()); // a new document, not a fabricated saved checkpoint
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneIOTest, SaveBeforeNewWaitsForAtomicWriteAndRetainsTheSavedContent)
{
    ASSERT_TRUE(Save(Path));
    const auto before = SavedSource(Path);
    Edit(6.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_TRUE(App->SceneChangePromptState().Saving);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(SavedSource(Path), before);
    ASSERT_TRUE(ProcessSave());
    EXPECT_FALSE(Owner.IsDirty());
    const auto saved = SavedSource(Path);
    EXPECT_NE(saved, before);
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &document, &Diagnostics));
    Scene reloaded = {};
    reloaded.Initialize(&Memory.MainArena);
    ASSERT_TRUE(document.PopulateCandidate(reloaded, Schemas, &Diagnostics));
    ASSERT_EQ(reloaded.AliveCount(), 1u);
    reloaded.ForEachEntity([&](EntityID id) {
        ASSERT_NE(reloaded.GetComponent<TransformComponent>(id), nullptr);
        EXPECT_FLOAT_EQ(reloaded.GetComponent<TransformComponent>(id)->Position.x, 6.f);
    });
    reloaded.Shutdown();
    EXPECT_EQ(ECS.AliveCount(), 1u);
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(ECS.AliveCount(), 0u);
    EXPECT_EQ(SavedSource(Path), saved);
}

TEST_F(EditorSceneIOTest, FailedSaveBeforeNewKeepsTheSceneHistoryAndOriginalFile)
{
    ASSERT_TRUE(Save(Path));
    const auto original = SavedSource(Path);
    Edit(8.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessNewScene());
    VFS.FailRename = true;
    EXPECT_FALSE(ProcessSave());
    EXPECT_EQ(SavedSource(Path), original);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(App->SceneChangePromptState().SaveFailed);
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(ECS.AliveCount(), 1u);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 8.f);
}

TEST_F(EditorSceneIOTest, SaveAsCancellationCancelsNewInsteadOfDiscardingOrRepromptingForever)
{
    Edit(9.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_EQ(Picker.Requests, 1u);
    EXPECT_TRUE(App->SceneChangePromptState().Saving);
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneIOTest, SaveAsSuccessDoesNotReplaceUntilItsExactEpochSaveCompletes)
{
    Edit(11.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessNewScene());
    const auto native = (Temporary / "project" / "Scenes" / "saved.zescene").string();
    Picker.Result.Complete(native);
    App->PollSceneSaveDialog();
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    ASSERT_TRUE(ProcessSave());
    const auto saved_path = VFSPath::Parse("/Scenes/saved.zescene").Value();
    EXPECT_TRUE(Exists(saved_path));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_NE(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ECS.AliveCount(), 0u);
}

TEST_F(EditorSceneIOTest, QueuedEditAfterDiscardResponseRequiresFreshPromptBeforeNew)
{
    Edit(3.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    LocalTransformValue value = {};
    value.Position.x          = 12.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, value));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 12.f);
    EXPECT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    EXPECT_FALSE(App->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
}

TEST_F(EditorSceneIOTest, LegacySceneSettingEditAfterDiscardResponseRequiresFreshConsent)
{
    EXPECT_EQ(Owner.ExternalRevision(), 0u);
    Owner.MarkDirty(true);
    const auto epoch    = App->Session.SceneEpoch();
    const auto revision = App->Session.DocumentRevision();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    Owner.AuthoredGrid.Origin.x = 12.f;
    Owner.MarkDirty(true);
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(ECS.AliveCount(), 1u);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.x, 12.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    EXPECT_EQ(App->SceneChangePromptState().ExternalRevision, Owner.ExternalRevision());
    EXPECT_FALSE(App->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
    const auto external_revision = Owner.ExternalRevision();
    Owner.MarkDirty(false);
    EXPECT_EQ(Owner.ExternalRevision(), external_revision);
}

TEST_F(EditorSceneIOTest, EditAfterCompletedSaveBeforeNewRetainsTheNewerContentAndSavedFile)
{
    ASSERT_TRUE(Save(Path));
    Edit(17.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(ProcessSave());
    ASSERT_FALSE(Owner.IsDirty());
    const auto saved = SavedSource(Path);
    Edit(18.f);
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 18.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(App->Session.History().UndoCount(), 2u);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
}

TEST_F(EditorSceneIOTest, SaveBeforeOpenReadsTheSavedContentAndRetiresHistoryOnlyOnPublication)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    Edit(19.f);
    const auto epoch = App->Session.SceneEpoch();
    const auto uuid  = App->Session.TargetFor(Entity).UUID;
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(ProcessOpen());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessOpen());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(App->SceneChangePromptState().Saving);
    ASSERT_TRUE(ProcessSave());
    EXPECT_FALSE(Owner.IsDirty());
    const auto saved = SavedSource(Path);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    FinishOpen();
    ASSERT_NE(Engine->Scene, &ECS);
    EXPECT_NE(App->Session.SceneEpoch(), epoch);
    const auto loaded = App->Session.Resolve(uuid);
    ASSERT_TRUE(Engine->Scene->IsAlive(loaded));
    ASSERT_NE(Engine->Scene->GetComponent<TransformComponent>(loaded), nullptr);
    EXPECT_FLOAT_EQ(Engine->Scene->GetComponent<TransformComponent>(loaded)->Position.x, 19.f);
    EXPECT_FALSE(App->Session.Dirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneIOTest, DiscardBeforeOpenPublishesOnlyAfterACompleteLoadAndDoesNotSaveTheOutgoingEdits)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    const auto saved = SavedSource(Path);
    Edit(13.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(ProcessOpen());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(ProcessOpen());
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_TRUE(Owner.IsDirty());
    FinishOpen();
    ASSERT_NE(Engine->Scene, &ECS);
    EXPECT_NE(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_FALSE(App->Session.Dirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneIOTest, FailedOpenAfterDiscardApprovalKeepsTheDirtySceneAndItsCheckpoint)
{
    LoadSlots();
    Edit(14.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(ProcessOpen());
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(ProcessOpen());
    FinishOpen();
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 14.f);
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneIOTest, EditAfterDiscardApprovalWhileOpenLoadsRequiresNewConsent)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    Edit(15.f);
    const auto epoch = App->Session.SceneEpoch();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(ProcessOpen());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(ProcessOpen());
    Edit(16.f); // before any publication, even if the worker has already finished
    ASSERT_TRUE(WaitForOpenPrompt());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 16.f);
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    EXPECT_FALSE(App->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneIOTest, LegacyEditAfterDiscardApprovalWhileOpenLoadsRequiresNewConsent)
{
    LoadSlots();
    ASSERT_TRUE(Save(Path));
    Owner.MarkDirty(true);
    const auto epoch    = App->Session.SceneEpoch();
    const auto revision = App->Session.DocumentRevision();
    ASSERT_TRUE(App->RequestSceneOpen(Path, epoch));
    ASSERT_TRUE(ProcessOpen());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(ProcessOpen());
    Owner.AuthoredGrid.Origin.x = 20.f;
    Owner.MarkDirty(true);
    ASSERT_TRUE(WaitForOpenPrompt());
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Engine->Scene, &ECS);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.x, 20.f);
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_TRUE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, CleanCloseApprovesOnlyAtTheMutationPointAndDoesNotTearDownTheWorld)
{
    EXPECT_FALSE(App->OnCloseRequested());
    EXPECT_FALSE(App->ProcessCloseRequest(*Engine));
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->RequestNewScene());
    EXPECT_FALSE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessClose());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_TRUE(App->Session.Ready());
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_EQ(Owner.Session, &App->Session);
}

TEST_F(EditorSceneIOTest, DirtyCloseCancelKeepsDocumentHistoryAndBothLoopsAlive)
{
    Edit(21.f);
    const auto epoch = App->Session.SceneEpoch();
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    const auto prompt = App->SceneChangePromptState();
    EXPECT_EQ(prompt.Kind, SceneChangeKind::CloseEditor);
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(Engine->RequestTerminate.value.load());
    EXPECT_FALSE(App->OnCloseRequested()); // repeated native close cannot replace consent
    ASSERT_TRUE(ProcessClose());
    EXPECT_EQ(App->SceneChangePromptState().Serial, prompt.Serial);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 21.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_TRUE(App->RequestNewScene()); // a cancelled close does not poison future requests
}

TEST_F(EditorSceneIOTest, DirtyCloseDiscardApprovesShutdownWithoutSavingOrDestroyingTheSourceEarly)
{
    ASSERT_TRUE(Save(Path));
    const auto saved = SavedSource(Path);
    Edit(22.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    ASSERT_TRUE(ProcessClose());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(App->Session.Ready());
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 22.f);
}

TEST_F(EditorSceneIOTest, SaveBeforeCloseWaitsForActualAtomicSuccess)
{
    ASSERT_TRUE(Save(Path));
    const auto original = SavedSource(Path);
    Edit(23.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessClose());
    EXPECT_TRUE(App->SceneChangePromptState().Saving);
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_EQ(SavedSource(Path), original);
    ASSERT_TRUE(ProcessSave());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_NE(SavedSource(Path), original);
    ASSERT_TRUE(ProcessClose());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorSceneIOTest, FailedSaveBeforeCloseCannotApproveShutdownOrLoseTheOriginalFile)
{
    ASSERT_TRUE(Save(Path));
    const auto saved = SavedSource(Path);
    Edit(24.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessClose());
    VFS.FailRename = true;
    EXPECT_FALSE(ProcessSave());
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(App->SceneChangePromptState().SaveFailed);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 24.f);
}

TEST_F(EditorSceneIOTest, CancelledSaveAsBeforeCloseCancelsTheTransition)
{
    Edit(25.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessClose());
    ASSERT_EQ(Picker.Requests, 1u);
    Picker.Result.Complete({});
    App->PollSceneSaveDialog();
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 25.f);
    ASSERT_TRUE(ProcessClose());
    EXPECT_EQ(Picker.Requests, 1u);
}

TEST_F(EditorSceneIOTest, NewEditsAfterCloseDiscardAndSaveRequireFreshConsent)
{
    ASSERT_TRUE(Save(Path));
    Edit(26.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    LocalTransformValue newer = {};
    newer.Position.x          = 27.f;
    ASSERT_TRUE(App->Session.RequestTransformValue(Entity, newer));
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(ProcessSave());
    const auto saved            = SavedSource(Path);
    Owner.AuthoredGrid.Origin.x = 28.f;
    Owner.MarkDirty(true);
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.x, 28.f);
    EXPECT_TRUE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, CloseRestoresAnUnsavedPreviewBeforeDeterminingTheCleanState)
{
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 29.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, CloseCannotReuseConsentFromAnotherSceneEpoch)
{
    Edit(30.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    const auto prompt = App->SceneChangePromptState();
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(nullptr));
    ASSERT_TRUE(App->Session.BindScene(&ECS, false));
    App->Session.EndMutationPhase();
    EXPECT_FALSE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_FALSE(App->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 30.f);
    EXPECT_TRUE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, NativeCloseDoesNotReplaceAnExistingLifecycleDecision)
{
    Edit(31.f);
    ASSERT_TRUE(App->RequestNewScene());
    ASSERT_TRUE(ProcessNewScene());
    const auto prompt = App->SceneChangePromptState();
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    EXPECT_EQ(App->SceneChangePromptState().Kind, SceneChangeKind::NewScene);
    EXPECT_EQ(App->SceneChangePromptState().Serial, prompt.Serial);
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    EXPECT_EQ(App->SceneChangePromptState().Kind, SceneChangeKind::CloseEditor);
    EXPECT_NE(App->SceneChangePromptState().Serial, prompt.Serial);
}

TEST_F(EditorSceneIOTest, NativeCloseDeliveryAndCleanApprovalWorkWithoutFocusOrAnOverlay)
{
    Engine->App = App.get();
    Engine->ApplicationCloseRequested.value.store(true);
    EXPECT_FALSE(ZEngine::Engine::ProcessApplicationCloseRequest(*Engine));
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    App->UpdateSessionInput(nullptr, false, Engine.get());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(App->Session.Ready());
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
}

TEST_F(EditorSceneIOTest, SaveAsCompletionAndCloseContinueWhileWindowIsUnfocused)
{
    Edit(32.f);
    EXPECT_FALSE(App->OnCloseRequested());
    App->UpdateSessionInput(nullptr, false, Engine.get());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    App->UpdateSessionInput(nullptr, false, Engine.get());
    ASSERT_EQ(Picker.Requests, 1u);
    EXPECT_TRUE(App->SceneChangePromptState().Saving);
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    const auto native = (Temporary / "project" / "Scenes" / "saved.zescene").string();
    ASSERT_TRUE(Picker.Result.Complete(native));
    App->PollSceneSaveDialog();
    App->UpdateSessionInput(nullptr, false, Engine.get());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(Owner.IsDirty());
    EXPECT_TRUE(Exists(VFSPath::Parse("/Scenes/saved.zescene").Value()));
    App->UpdateSessionInput(nullptr, false, Engine.get());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 32.f);
}

TEST_F(EditorSceneIOTest, UnfocusedCloseCancelLeavesHistoryAndSourceUntouched)
{
    Edit(33.f);
    EXPECT_FALSE(App->OnCloseRequested());
    App->UpdateSessionInput(nullptr, false, Engine.get());
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    App->UpdateSessionInput(nullptr, false, Engine.get());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 33.f);
}

TEST_F(EditorSceneIOTest, CloseCancellationWinsOverASuccessfulSaveAndCleanCheckpoint)
{
    ASSERT_TRUE(Save(Path));
    Edit(34.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Save));
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessSave());
    EXPECT_FALSE(Owner.IsDirty());
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_FALSE(App->SceneChangePromptState().Visible());
    EXPECT_TRUE(App->Session.Ready());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 34.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorSceneIOTest, ApprovedDiscardDoesNotProcessAnotherQueuedSaveAfterTheCloseBoundary)
{
    ASSERT_TRUE(Save(Path));
    const auto saved = SavedSource(Path);
    Edit(35.f);
    EXPECT_FALSE(App->OnCloseRequested());
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(Answer(SceneChangeChoice::Discard));
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    App->UpdateSessionInput(nullptr, true, Engine.get());
    EXPECT_TRUE(Engine->CloseRequested.value.load());
    EXPECT_EQ(SavedSource(Path), saved);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_FLOAT_EQ(ECS.GetComponent<TransformComponent>(Entity)->Position.x, 35.f);
}

TEST_F(EditorSceneIOTest, EditorSaveDrainsPendingSkyAndGridIntoTheCommittedSnapshot)
{
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 8.f;
    sky.Sphere.ShowSunDisc   = false;
    auto grid                = Owner.AuthoredGrid;
    grid.BaseSpacing         = .25f;
    grid.Origin.y            = 7.f;
    grid.MajorLineEvery      = 12;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessSave());
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &document, &Diagnostics));
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_EQ(document.Settings()->Sky, sky);
    EXPECT_TRUE(EqualSceneGridSettings(document.Settings()->Grid, grid));
    EXPECT_EQ(Owner.Sky, sky);
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 7.f);
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, EarlierSettingsDirtyACleanSceneBeforeNewEvaluatesConsent)
{
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 8.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestNewScene());
    EXPECT_FALSE(App->RequestSceneSkySettings({}));
    EXPECT_FALSE(App->RequestSceneGridSettings({}));
    ASSERT_TRUE(ProcessNewScene());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_EQ(Owner.Sky, sky);
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessNewScene());
    EXPECT_EQ(Owner.Sky, sky);
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneIOTest, GridOnlySaveUsesSharedHistoryAndKeepsItsUndoableSavedCheckpoint)
{
    const auto before   = Owner.AuthoredGrid;
    auto       grid     = before;
    grid.Origin         = {4.f, 7.f, 9.f};
    grid.Orientation    = {.2f, -.3f, .4f, .5f};
    grid.MajorLineEvery = 7;
    grid.BaseSpacing    = .125f;
    grid.VColor         = {.1f, .2f, .3f, .4f};
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(App->RequestSceneSave(Path, App->Session.SceneEpoch()));
    ASSERT_TRUE(ProcessSave());
    const auto saved = Owner.AuthoredGrid;
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(Owner.ExternalRevision(), 0u);
    EXPECT_FALSE(Owner.IsDirty());
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Read(VFS, Path, Schemas, &document, &Diagnostics));
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_TRUE(EqualSceneGridSettings(document.Settings()->Grid, saved));
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    App->Session.EndMutationPhase();
    EXPECT_TRUE(EqualSceneGridSettings(Owner.AuthoredGrid, before));
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Redo());
    App->Session.EndMutationPhase();
    EXPECT_TRUE(EqualSceneGridSettings(Owner.AuthoredGrid, saved));
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneIOTest, EarlierSettingsDirtyACleanSceneBeforeCloseEvaluatesConsent)
{
    auto grid    = Owner.AuthoredGrid;
    grid.Enabled = false;
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    EXPECT_FALSE(App->OnCloseRequested());
    EXPECT_FALSE(App->RequestSceneSkySettings({}));
    ASSERT_TRUE(ProcessClose());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_FALSE(Owner.AuthoredGrid.Enabled);
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessClose());
    EXPECT_FALSE(Engine->CloseRequested.value.load());
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneIOTest, EarlierSettingsDirtyACleanSceneBeforeOpenEvaluatesConsent)
{
    ASSERT_TRUE(Save(Path));
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 8.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneOpen(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestSceneSkySettings({}));
    ASSERT_TRUE(ProcessOpen());
    ASSERT_TRUE(App->SceneChangePromptState().Visible());
    EXPECT_EQ(Owner.Sky, sky);
    ASSERT_TRUE(Answer(SceneChangeChoice::Cancel));
    ASSERT_TRUE(ProcessOpen());
    EXPECT_FALSE(App->SceneOpenPending());
    EXPECT_TRUE(ECS.IsAlive(Entity));
}

TEST_F(EditorSceneIOTest, DirectCandidatePublicationCannotDiscardAnEarlierPendingSetting)
{
    LoadSlots();
    std::unique_ptr<EditorSceneCandidate> candidate = {};
    ASSERT_TRUE(Prepare(&candidate));
    const auto epoch         = App->Session.SceneEpoch();
    auto       sky           = Owner.Sky;
    sky.EnvironmentIntensity = 8.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    EXPECT_FALSE(Publish(candidate, epoch));
    EXPECT_NE(candidate, nullptr);
    EXPECT_EQ(App->Session.SceneEpoch(), epoch);
    EXPECT_EQ(Owner.Sky, sky);
    EXPECT_TRUE(ECS.IsAlive(Entity));
    EXPECT_TRUE(Owner.IsDirty());
}

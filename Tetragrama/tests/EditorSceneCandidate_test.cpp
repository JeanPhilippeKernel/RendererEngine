#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorCookedSceneRequest.h>
#include <Tetragrama/EditorSceneCandidate.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/CookedRuntimeReconstruction.h>
#include <ZEngine/Applications/CookedRuntimeSceneHost.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <ZEngine/Rendering/Scenes/CookedRuntimeScene.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <array>
#include <thread>
#include "../../ZEngine/tests/Cook/CookTestAllocator.h"

using namespace Tetragrama;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorSceneCandidateTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager                       Memory             = {};
    std::unique_ptr<ZEngine::EngineContext>                    Engine             = {};
    Scene                                                      Source             = {};
    SceneComponentSchemaRegistry                               Schemas            = {};
    SceneDiagnostics                                           Diagnostics        = {};
    SceneDocumentSettings                                      Settings           = {};
    std::unique_ptr<EditorSceneCandidate>                      Candidate          = {};
    std::unique_ptr<ZEngine::Rendering::RenderResourceManager> ResourceService    = {};
    VFSPath                                                    Path               = VFSPath::Parse("/Scenes/loaded.zescene").Value();
    uint32_t                                                   ResolveCalls       = 0;
    uint32_t                                                   Draws              = 1;
    bool                                                       AssetExists        = true;
    bool                                                       FailWithDiagnostic = false;

    static uuids::uuid                                         Identity(uint32_t index)
    {
        std::array<uint8_t, 16> bytes = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x47, 0x88, 0x99, 0xaa, 0xbb, 0xcc};
        for (uint32_t i = 0; i < 4; ++i)
        {
            bytes[12 + i] = static_cast<uint8_t>(index >> (i * 8));
        }
        return uuids::uuid(bytes);
    }
    void SetUp() override
    {
        Memory.Initialize(ZMega(192), {});
        Engine = std::make_unique<ZEngine::EngineContext>();
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaA, "CandidateTest/A");
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaB, "CandidateTest/B");
        Source.Initialize(&Memory.MainArena);
        Engine->Scene = &Source;
        Schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
        Settings.UUID = Identity(100000);
        Settings.Name = "Owned candidate name";
    }
    void TearDown() override
    {
        Candidate.reset();
        ResourceService.reset();
        Source.Shutdown();
        Engine.reset();
    }
    EntityID AddEntity(uint32_t index, bool mesh = false)
    {
        const auto entity = Source.CreateEntity();
        Source.AddComponent<UUIDComponent>(entity, {Identity(index)});
        TransformComponent transform = {};
        transform.Position           = {float(index), 2.f, 3.f};
        Source.AddComponent<TransformComponent>(entity, transform);
        if (mesh)
        {
            Source.AddComponent<MeshComponent>(entity, {Identity(500000), 12345});
        }
        return entity;
    }
    EditorSceneAssetResolver Resolver()
    {
        return {this, [](void* context, const uuids::uuid& uuid, uint32_t* out_draws, SceneDiagnostics* diagnostics) {
                    auto& fixture = *static_cast<EditorSceneCandidateTest*>(context);
                    ++fixture.ResolveCalls;
                    EXPECT_FALSE(uuid.is_nil());
                    if (fixture.FailWithDiagnostic)
                    {
                        SceneDiagnosticError(diagnostics, "Asset lookup failed.");
                        return false;
                    }
                    *out_draws = fixture.Draws;
                    return fixture.AssetExists;
                }};
    }
    bool Capture(SceneDocument* document)
    {
        return SceneDocument::Capture(Source, Settings, Schemas, document, &Diagnostics);
    }
    bool Prepare(SceneDocument&& document, const EditorSceneCandidateLimits& limits = {})
    {
        return EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, Resolver(), &Candidate, &Diagnostics, limits);
    }
    bool AnySlotBusy() const
    {
        return Engine->EditorSceneLoadArenaAInUse.value.load() || Engine->EditorSceneLoadArenaBInUse.value.load();
    }
};

TEST_F(EditorSceneCandidateTest, RenderPreparationCopiesTheCandidatesResolvedSunInsteadOfOutgoingLightState)
{
    const auto light = AddEntity(7);
    Source.AddComponent<LightComponent>(light, {.LightType = LightComponent::Type::Directional});
    Settings.Sky.PrimaryCelestialLight = Identity(7);
    SceneDocument document             = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_FALSE(Candidate->Render().CelestialLight.IsAvailable);
    ResourceService = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
    ASSERT_TRUE(Candidate->PrepareRenderResources(*ResourceService, {1, 1}, 1));
    ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(Candidate->PollRenderResources(&result));
    ZEngine::Rendering::RenderResourceManager::SceneEnvironmentPreparation environment = {};
    ASSERT_TRUE(ResourceService->GetSceneEnvironmentPreparation(result.Id, &environment));
    EXPECT_TRUE(environment.Input.CelestialLight.IsAvailable);
    EXPECT_FLOAT_EQ(environment.Input.CelestialLight.DirectionToLight[0], 0.0f);
    EXPECT_FLOAT_EQ(environment.Input.CelestialLight.DirectionToLight[1], 0.0f);
    EXPECT_FLOAT_EQ(environment.Input.CelestialLight.DirectionToLight[2], -1.0f);
    EXPECT_EQ(environment.Input.Sky.PrimaryCelestialLight, Identity(7));
    EXPECT_EQ(environment.Input.SkyRevision, Candidate->Render().SkyRevision.value.load());
    EXPECT_EQ(result.Environment, ZEngine::Rendering::RenderResourceManager::SceneEnvironmentPreparationState::Queued);
}

TEST_F(EditorSceneCandidateTest, RenderPreparationRejectsUnverifiedCookedClosureBeforeAdmission)
{
    AddEntity(7, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    ResourceService                                       = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
    ZEngine::Managers::CookedCpuAssetSet unverified       = {};
    bool                                 terminal_failure = false;
    EXPECT_FALSE(Candidate->PrepareRenderResources(*ResourceService, {1, 1}, 1, {}, &unverified, &terminal_failure));
    EXPECT_TRUE(terminal_failure);
    ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(Candidate->PollRenderResources(&result));
    EXPECT_TRUE(Candidate->PrepareRenderResources(*ResourceService, {1, 1}, 1));
    EXPECT_EQ(Candidate->RequiredMeshes().size(), 1u);
}

TEST_F(EditorSceneCandidateTest, RenderPreparationReportsMissingInstanceTransformAsTerminal)
{
    AddEntity(7, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    ASSERT_EQ(Candidate->Render().Instances.size(), 1u);
    Candidate->World().RemoveComponent<TransformComponent>(Candidate->Render().Instances[0].Entity);
    ResourceService       = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
    bool terminal_failure = false;
    EXPECT_FALSE(Candidate->PrepareRenderResources(*ResourceService, {1, 1}, 1, {}, nullptr, &terminal_failure));
    EXPECT_TRUE(terminal_failure);
    ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(Candidate->PollRenderResources(&result));
}

TEST_F(EditorSceneCandidateTest, AdmittedSlotTransfersToCandidateWithoutASecondReservation)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    SceneStorageLease admitted = {}, other = {};
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&admitted));
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&other));
    auto* expected = admitted.Arena();
    ASSERT_TRUE(EditorSceneCandidate::Prepare(std::move(admitted), std::move(document), Schemas, Path, Resolver(), &Candidate, &Diagnostics));
    EXPECT_EQ(admitted.Arena(), nullptr);
    EXPECT_EQ(Candidate->Render().LocalArena, expected);
    EXPECT_TRUE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_TRUE(Engine->EditorSceneLoadArenaBInUse.value.load());
    Candidate.reset();
    EXPECT_FALSE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_TRUE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

TEST_F(EditorSceneCandidateTest, PopulatedAdapterCanBePreparedByItsExclusiveWorkerWithoutChangingActiveBindings)
{
    AddEntity(7, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    SceneStorageLease storage = {};
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&storage));
    std::unique_ptr<SceneInstance> instance = {};
    ASSERT_TRUE(SceneInstance::Create(std::move(storage), &instance, &Diagnostics));
    ASSERT_TRUE(instance->Populate(std::move(document), Schemas, &Diagnostics));
    const auto                     owner_thread    = std::this_thread::get_id();
    std::thread::id                resolver_thread = {};
    const EditorSceneAssetResolver resolver        = {&resolver_thread, [](void* context, const uuids::uuid&, uint32_t* draws, SceneDiagnostics*) {
                                                   *static_cast<std::thread::id*>(context) = std::this_thread::get_id();
                                                   *draws                                  = 1;
                                                   return true;
                                                      }};
    bool                           prepared        = false;
    std::thread                    worker([&] { prepared = EditorSceneCandidate::PreparePopulated(std::move(instance), Path, resolver, &Candidate, &Diagnostics); });
    worker.join();
    ASSERT_TRUE(prepared);
    EXPECT_NE(resolver_thread, owner_thread);
    EXPECT_EQ(Engine->Scene, &Source);
    EXPECT_EQ(Source.AliveCount(), 1u);
    ASSERT_NE(Candidate, nullptr);
    ASSERT_EQ(Candidate->Render().Instances.size(), 1u);
    EXPECT_FLOAT_EQ(Candidate->Render().Instances[0].Transform(0, 3), 7.f);
    EXPECT_EQ(Candidate->Actors().Count(), 0u);
    EXPECT_EQ(Candidate->Render().Session, nullptr);
    EXPECT_TRUE(AnySlotBusy());
    Candidate.reset();
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, CookedAdapterRejectsUnverifiedClosureWithoutConsumingTheScene)
{
    AddEntity(7, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    SceneStorageLease storage = {};
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&storage));
    std::unique_ptr<SceneInstance> instance = {};
    ASSERT_TRUE(SceneInstance::Create(std::move(storage), &instance, &Diagnostics));
    ASSERT_TRUE(instance->Populate(std::move(document), Schemas, &Diagnostics));
    ZEngine::Managers::CookedCpuAssetSet                            unverified    = {};
    std::unique_ptr<ZEngine::Rendering::Scenes::CookedRuntimeScene> runtime_scene = {};
    EXPECT_FALSE(ZEngine::Rendering::Scenes::CookedRuntimeScene::Prepare(std::move(instance), unverified, &runtime_scene, &Diagnostics));
    EXPECT_NE(instance, nullptr);
    EXPECT_EQ(runtime_scene, nullptr);
    EXPECT_FALSE(EditorSceneCandidate::PreparePopulatedCooked(std::move(instance), Path, unverified, &Candidate, &Diagnostics));
    EXPECT_NE(instance, nullptr);
    EXPECT_EQ(Candidate, nullptr);
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, CookedReconstructionAdapterRejectsMissingWorkWithoutChangingOutput)
{
    ZEngine::ECS::CookedSceneReconstruction reconstruction = {};
    auto                                    error          = ZEngine::Core::VFS::VFSError::PermissionDenied;
    EXPECT_EQ(EditorSceneCandidate::PrepareReconstructedCooked(reconstruction, Path, &Candidate, &Diagnostics, &error), CookedCandidateState::Failed);
    EXPECT_EQ(Candidate, nullptr);
    EXPECT_EQ(error, ZEngine::Core::VFS::VFSError::PermissionDenied);
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, VerifiedCookedReconstructionWaitsForAssetsAndTransfersToCandidate)
{
    namespace Cook = ZEngine::Cook;
    namespace VFS  = ZEngine::Core::VFS;
    using JSON     = nlohmann::json;
    AddEntity(7);
    ASSERT_TRUE(Schemas.FreezeForWorker(&Diagnostics));
    SceneDocument source = {};
    ASSERT_TRUE(Capture(&source));
    ZEngine::Core::Containers::Array<uint8_t> bytes = {};
    ASSERT_TRUE(BinarySceneCodec::Encode(source, Schemas, &Memory.MainArena, &bytes, &Diagnostics));
    auto                           allocator = std::make_unique<Cook::Tests::FixedAllocator<ZKilo(256), 12>>();
    Cook::CookCodecAllocationRoute route(*allocator);
    VFS::VFSMemoryBackend          backend = {};
    ASSERT_TRUE(backend.InitializeOwned(ZMega(1), 32).Succeeded());
    ASSERT_TRUE(backend.CreateDir(VFS::VFSPath::Parse("/package").Value()).Succeeded());
    ASSERT_TRUE(backend.CreateDir(VFS::VFSPath::Parse("/package/objects").Value()).Succeeded());
    VFS::VFSContext context = {};
    context.Initialize(&Memory.MainArena);
    ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
    struct ContextGuard
    {
        VFS::VFSContext& Context;
        ~ContextGuard()
        {
            Context.Shutdown();
        }
    } context_guard{context};
    ZEngine::Core::Hash::SHA256 hash   = {};
    Cook::Digest256             digest = {};
    ASSERT_TRUE(hash.Update({bytes.data(), bytes.size()}).Succeeded());
    ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
    Cook::CookObjectDescriptor object = {};
    ASSERT_TRUE(Cook::MakeCookObjectDescriptor(digest, bytes.size(), Cook::CookSceneObjectExtension, &object).Succeeded());
    const auto object_path = std::string("/package/") + object.Path.CStr();
    ASSERT_TRUE(backend.WriteFile(VFS::VFSPath::Parse(object_path.c_str()).Value(), {bytes.data(), bytes.size()}).Succeeded());
    const auto words = Cook::CookSchemaKeyWorkspaceWords(Schemas);
    ASSERT_TRUE(words.Succeeded());
    std::vector<uint32_t> workspace(words.Value());
    Cook::Digest256       schema_key = {};
    ASSERT_TRUE(Cook::ComputeCookSchemaKey(*allocator, Schemas, {workspace.data(), workspace.size()}, &schema_key).Succeeded());
    const auto object_hex = digest.Hex();
    const auto schema_hex = schema_key.Hex();
    JSON       manifest   = {
        {"manifest_version", 1},
        {"recipe_revision", 1},
        {"target", "desktop-vulkan-le64"},
        {"settings", JSON::object()},
        {"settings_key", "ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356"},
        {"schema_key", std::string(schema_hex.data(), 64)},
        {"assets", JSON::array()},
        {"scenes", JSON::array({{{"uuid", uuids::to_string(Settings.UUID)}, {"source", "scenes/loaded.zescene"}, {"source_digest", std::string(64, '1')}, {"input_key", std::string(64, '2')}, {"object", {{"path", object.Path.CStr()}, {"digest", std::string(object_hex.data(), 64)}, {"size", object.Size}}}, {"roots", JSON::array()}}})}
    };
    const auto manifest_text = manifest.dump();
    ASSERT_TRUE(backend.WriteFile(VFS::VFSPath::Parse("/package/manifest.json").Value(), {reinterpret_cast<const uint8_t*>(manifest_text.data()), manifest_text.size()}).Succeeded());
    VFS::VFSReadRootLease root = {};
    ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Parse("/package").Value(), &root).Succeeded());
    ZEngine::Managers::CookedPackageSnapshot package          = {};
    Cook::CookDiagnostics                    cook_diagnostics = {};
    const Cook::CookLimits                   cook_limits      = {.MaxWorkingBytes = ZKilo(256)};
    const auto                               captured         = ZEngine::Managers::CookedPackageSnapshot::Capture(std::move(root), Settings.UUID, Schemas, cook_limits, *allocator, &cook_diagnostics, &package);
    ASSERT_TRUE(captured.Succeeded()) << "error " << static_cast<uint32_t>(captured.Error()) << ", diagnostic " << (cook_diagnostics.Count ? cook_diagnostics.Entries[0].Message : "none");
    ZEngine::Managers::CookedCpuClosureCache      cache  = {};
    const ZEngine::Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = ZKilo(64), .MaxMeshWorkspaceBytes = ZKilo(64)};
    ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 1, 6, 1));
    struct CacheGuard
    {
        ZEngine::Managers::CookedCpuClosureCache& Cache;
        ~CacheGuard()
        {
            while (Cache.PumpOne())
            {
            }
            EXPECT_TRUE(Cache.Shutdown());
        }
    } cache_guard{cache};
    SceneStorageLease storage = {};
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&storage));
    CookedSceneReconstruction reconstruction = {};
    ASSERT_EQ(ReconstructCookedScene(package, std::move(storage), Schemas, route, ZKilo(64), ZMega(4), cache, &reconstruction, &Diagnostics), CookedSceneReconstructionError::None);
    EXPECT_EQ(EditorSceneCandidate::PrepareReconstructedCooked(reconstruction, Path, &Candidate, &Diagnostics), CookedCandidateState::Pending);
    ASSERT_NE(reconstruction.Instance, nullptr);
    EXPECT_EQ(Candidate, nullptr);
    {
        // The same verified package can build an independent renderer-only
        // scene owner without constructing an editor adapter or session.
        VFS::VFSReadRootLease runtime_root = {};
        ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Parse("/package").Value(), &runtime_root).Succeeded());
        SceneStorageLease runtime_storage = {};
        ASSERT_TRUE(Engine->TryAcquireSceneStorage(&runtime_storage));
        ZEngine::Managers::CookedCpuClosureCache::Lease                 runtime_lease = {};
        std::unique_ptr<ZEngine::Rendering::Scenes::CookedRuntimeScene> runtime_scene = {};
        std::atomic<bool>                                               cancelled     = true;
        using RuntimeReconstructionError                                              = ZEngine::Applications::CookedRuntimeReconstructionError;
        EXPECT_EQ(ZEngine::Applications::ReconstructCookedRuntimeScene(std::move(runtime_root), Settings.UUID, std::move(runtime_storage), Schemas, cook_limits, *allocator, route, ZKilo(64), ZMega(4), cache, &runtime_scene, &runtime_lease, &cook_diagnostics, &Diagnostics, &cancelled), RuntimeReconstructionError::Cancelled);
        EXPECT_TRUE(runtime_root.IsValid());
        EXPECT_NE(runtime_storage.Arena(), nullptr);
        EXPECT_EQ(runtime_scene, nullptr);
        EXPECT_FALSE(runtime_lease);
        cancelled.store(false);
        ASSERT_EQ(ZEngine::Applications::ReconstructCookedRuntimeScene(std::move(runtime_root), Settings.UUID, std::move(runtime_storage), Schemas, cook_limits, *allocator, route, ZKilo(64), ZMega(4), cache, &runtime_scene, &runtime_lease, &cook_diagnostics, &Diagnostics, &cancelled), RuntimeReconstructionError::None);
        ZEngine::Managers::CookedCpuClosureCache::Result runtime_assets = {};
        ASSERT_TRUE(runtime_lease.Poll(&runtime_assets));
        ASSERT_EQ(runtime_assets.StateValue, ZEngine::Managers::CookedCpuClosureCache::State::Ready);
        ASSERT_NE(runtime_assets.Assets, nullptr);
        ASSERT_NE(runtime_scene, nullptr);
        EXPECT_EQ(runtime_scene->Instance().World().AliveCount(), 1u);
        EXPECT_EQ(runtime_scene->DrawCount(), 0u);
        EXPECT_EQ(runtime_scene->Render().Instances.size(), 0u);
        EXPECT_EQ(Engine->Scene, &Source);
        auto runtime_resources                                      = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
        auto runtime_device                                         = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        runtime_device->RRM                                         = runtime_resources.get();
        auto runtime_app                                            = std::make_unique<Editor>();
        auto runtime_pipeline                                       = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
        runtime_app->RenderPipeline                                 = runtime_pipeline.get();
        Engine->RenderResourceManager                               = runtime_resources.get();
        ZEngine::Applications::CookedRuntimeSceneHost runtime_host  = {};
        ZEngine::Applications::RenderFrameState       runtime_frame = {};
        runtime_frame.RenderTargetW                                 = 1280;
        runtime_frame.RenderTargetH                                 = 720;
        EXPECT_FALSE(runtime_host.Publish(*Engine, *runtime_app, runtime_scene, {5, 1}, Engine->ScenePublication.Generation(), {}, runtime_frame, &Diagnostics));
        ASSERT_NE(runtime_scene, nullptr);
        EXPECT_EQ(Engine->Scene, &Source);
        EXPECT_EQ(Engine->ScenePublication.Generation(), 1u);
        EXPECT_EQ(runtime_app->CurrentScene, nullptr);
        ZEngine::Core::Memory::TLSFSlab runtime_metadata = {};
        runtime_metadata.Init(&Memory.MainArena, ZKilo(16));
        ZEngine::Rendering::CookedSceneGpuMailbox              runtime_mailbox = {};
        ZEngine::Rendering::Scenes::CookedSceneResourceRequest runtime_request = {};
        using RuntimeRequest                                                   = ZEngine::Rendering::Scenes::CookedSceneResourceRequest;
        ASSERT_TRUE(runtime_request.Begin(runtime_scene->Instance(), runtime_scene->Render(), runtime_scene->Resources(), runtime_scene->RequiredMeshes(), std::move(runtime_lease), {5, 1}, 1, {}, route, runtime_metadata, *runtime_resources, *runtime_device, runtime_mailbox, 0));
        ASSERT_EQ(runtime_request.Tick(), RuntimeRequest::State::GpuPending);
        for (uint32_t i = 0; i < 8 && runtime_request.Status() == RuntimeRequest::State::GpuPending; ++i)
        {
            ASSERT_TRUE(runtime_mailbox.PumpOne(0, 0));
            (void) runtime_request.Tick();
        }
        ASSERT_EQ(runtime_request.Status(), RuntimeRequest::State::ResourcePending);
        runtime_request.Cancel();
        ASSERT_EQ(runtime_request.Status(), RuntimeRequest::State::Cancelled);
        runtime_scene.reset();
        runtime_request.ReleaseTerminalAssets();
        EXPECT_TRUE(runtime_request.Reset());
        Engine->RenderResourceManager = nullptr;
    }
    ASSERT_EQ(EditorSceneCandidate::PrepareReconstructedCooked(reconstruction, Path, &Candidate, &Diagnostics), CookedCandidateState::Ready);
    EXPECT_EQ(reconstruction.Instance, nullptr);
    EXPECT_TRUE(reconstruction.Assets);
    ASSERT_NE(Candidate, nullptr);
    EXPECT_EQ(Candidate->World().AliveCount(), 1u);
    EXPECT_EQ(Engine->Scene, &Source);
    ZEngine::Managers::CookedCpuClosureCache::Result assets = {};
    ASSERT_TRUE(reconstruction.Assets.Poll(&assets));
    ASSERT_NE(assets.Assets, nullptr);
    ResourceService                          = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
    auto device                              = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->RRM                              = ResourceService.get();
    ZEngine::Core::Memory::TLSFSlab metadata = {};
    metadata.Init(&Memory.MainArena, ZKilo(16));
    ZEngine::Rendering::CookedSceneGpuMailbox mailbox = {};
    EditorCookedSceneRequest                  request = {};
    ASSERT_TRUE(request.Begin(Candidate, std::move(reconstruction.Assets), {4, 1}, 1, {}, route, metadata, *ResourceService, *device, mailbox, 0));
    EXPECT_EQ(Candidate, nullptr);
    ASSERT_EQ(request.Tick(), EditorCookedSceneRequest::State::GpuPending);
    for (uint32_t i = 0; i < 8 && request.Status() == EditorCookedSceneRequest::State::GpuPending; ++i)
    {
        ASSERT_TRUE(mailbox.PumpOne(0, 0));
        (void) request.Tick();
    }
    ASSERT_EQ(request.Status(), EditorCookedSceneRequest::State::ResourcePending);
    EXPECT_TRUE(mailbox.Drained());
    request.Cancel();
    EXPECT_EQ(request.Status(), EditorCookedSceneRequest::State::Cancelled);
    EXPECT_TRUE(request.Reset());
    ASSERT_TRUE(cache.PumpOne());

    // Exercise the editor's actual request ledger and cancellation before a
    // drawable viewport exists. No authoring VFS or asset registry is used.
    VFS::VFSReadRootLease second_root = {};
    ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Parse("/package").Value(), &second_root).Succeeded());
    ZEngine::Managers::CookedPackageSnapshot second_package = {};
    ASSERT_TRUE(ZEngine::Managers::CookedPackageSnapshot::Capture(std::move(second_root), Settings.UUID, Schemas, cook_limits, *allocator, &cook_diagnostics, &second_package).Succeeded());
    ASSERT_TRUE(Engine->TryAcquireSceneStorage(&storage));
    CookedSceneReconstruction second = {};
    ASSERT_EQ(ReconstructCookedScene(second_package, std::move(storage), Schemas, route, ZKilo(64), ZMega(4), cache, &second, &Diagnostics), CookedSceneReconstructionError::None);
    ASSERT_TRUE(cache.PumpOne());
    const auto cooked_source = VFS::VFSPath::Parse("/scenes/fixture.scene").Value();
    ASSERT_EQ(EditorSceneCandidate::PrepareReconstructedCooked(second, cooked_source, &Candidate, &Diagnostics), CookedCandidateState::Ready);
    EXPECT_TRUE(Candidate->Render().SourceReadOnly);
    {
        Editor                                   editor   = {};
        EditorScene                              outgoing = {};
        ZEngine::Applications::AppRenderPipeline pipeline = {};
        pipeline.Device                                   = device.get();
        editor.RenderPipeline                             = &pipeline;
        editor.CurrentScene                               = &outgoing;
        Engine->RenderResourceManager                     = ResourceService.get();
        ASSERT_TRUE(editor.Session.Initialize(&Memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(editor.Session.BeginMutationPhase());
        ASSERT_TRUE(editor.Session.BindScene(&Source));
        ASSERT_TRUE(outgoing.AttachSession(editor.Session, *Engine));
        editor.Session.EndMutationPhase();
        SceneRequestId id = {};
        ASSERT_TRUE(editor.RequestCookedSceneOpen(Candidate, std::move(second.Assets), editor.Session.SceneEpoch(), route, metadata, 0, &id));
        EXPECT_EQ(Candidate, nullptr);
        EXPECT_TRUE(editor.SceneOpenPending());
        EXPECT_EQ(Engine->Scene, &Source);
        EXPECT_EQ(Engine->SceneLoads.Cancel(id), SceneCancelResult::Cancelled);
        ASSERT_TRUE(editor.Session.BeginMutationPhase());
        EXPECT_FALSE(editor.ProcessSceneOpen(*Engine));
        editor.Session.EndMutationPhase();
        EXPECT_FALSE(editor.SceneOpenPending());
        SceneLoadResult result = {};
        ASSERT_TRUE(Engine->SceneLoads.Poll(id, &result));
        EXPECT_EQ(result.Outcome, SceneLoadOutcome::Cancelled);
        EXPECT_FALSE(result.CleanupPending);
        EXPECT_TRUE(Engine->SceneLoads.Acknowledge(id));

        // A source request can supersede a cooked candidate. The new request
        // waits until the old candidate's cleanup releases its coordinator slot.
        while (cache.PumpOne())
        {
        }
        VFS::VFSReadRootLease third_root = {};
        ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Parse("/package").Value(), &third_root).Succeeded());
        ZEngine::Managers::CookedPackageSnapshot third_package = {};
        ASSERT_TRUE(ZEngine::Managers::CookedPackageSnapshot::Capture(std::move(third_root), Settings.UUID, Schemas, cook_limits, *allocator, &cook_diagnostics, &third_package).Succeeded());
        ASSERT_TRUE(Engine->TryAcquireSceneStorage(&storage));
        CookedSceneReconstruction third = {};
        ASSERT_EQ(ReconstructCookedScene(third_package, std::move(storage), Schemas, route, ZKilo(64), ZMega(4), cache, &third, &Diagnostics), CookedSceneReconstructionError::None);
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_EQ(EditorSceneCandidate::PrepareReconstructedCooked(third, cooked_source, &Candidate, &Diagnostics), CookedCandidateState::Ready);
        SceneRequestId cooked = {}, source_request = {};
        ASSERT_TRUE(editor.RequestCookedSceneOpen(Candidate, std::move(third.Assets), editor.Session.SceneEpoch(), route, metadata, 0, &cooked));
        ASSERT_TRUE(editor.RequestSceneOpen(Path, editor.Session.SceneEpoch(), &source_request));
        ASSERT_TRUE(Engine->SceneLoads.Poll(cooked, &result));
        EXPECT_EQ(result.Outcome, SceneLoadOutcome::Superseded);
        EXPECT_TRUE(result.CleanupPending);
        EXPECT_FALSE(Engine->SceneLoads.TryStart(source_request));
        ASSERT_TRUE(editor.Session.BeginMutationPhase());
        EXPECT_FALSE(editor.ProcessSceneOpen(*Engine));
        editor.Session.EndMutationPhase();
        ASSERT_TRUE(Engine->SceneLoads.Poll(cooked, &result));
        EXPECT_EQ(result.Outcome, SceneLoadOutcome::Superseded);
        EXPECT_FALSE(result.CleanupPending);
        EXPECT_EQ(Engine->Scene, &Source);
        EXPECT_TRUE(Engine->SceneLoads.Acknowledge(cooked));
        EXPECT_EQ(Engine->SceneLoads.Cancel(source_request), SceneCancelResult::Cancelled);
        ASSERT_TRUE(editor.Session.BeginMutationPhase());
        EXPECT_TRUE(editor.ProcessSceneOpen(*Engine));
        editor.Session.EndMutationPhase();
        EXPECT_FALSE(editor.SceneOpenPending());
        EXPECT_TRUE(Engine->SceneLoads.Acknowledge(source_request));
        editor.OnWorldShutdown(*Engine);
        editor.CurrentScene = nullptr;
    }
    metadata.Shutdown();
    ASSERT_TRUE(cache.PumpOne());
    EXPECT_TRUE(cache.Shutdown());
}

TEST_F(EditorSceneCandidateTest, EmptyDocumentHasOwnedMetadataAndNoDefaultContent)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    ASSERT_NE(Candidate, nullptr);
    EXPECT_EQ(Candidate->World().AliveCount(), 0u);
    EXPECT_EQ(Candidate->Actors().Count(), 0u);
    EXPECT_EQ(Candidate->Render().Instances.size(), 0u);
    EXPECT_EQ(Candidate->DrawCount(), 0u);
    EXPECT_STREQ(Candidate->Render().Name, Settings.Name.c_str());
    EXPECT_EQ(Candidate->Render().UUID, Settings.UUID);
    EXPECT_EQ(Candidate->Render().SourcePath, Path);
    EXPECT_FALSE(Candidate->Render().SourceReadOnly);
    EXPECT_FALSE(Candidate->Render().IsDirty());
    EXPECT_EQ(document.Settings(), nullptr);
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_EQ(Engine->Scene, &Source);
    EXPECT_EQ(Engine->ActorManager, nullptr);
}

TEST_F(EditorSceneCandidateTest, LegacyDirectionalLightMeshIsRemovedBeforeAssetResolution)
{
    const auto light = AddEntity(1);
    Source.AddComponent<LightComponent>(light, {.LightType = LightComponent::Type::Directional});
    const auto legacy_icon = uuids::uuid::from_string("ff000000-0000-0000-0000-000000000001");
    ASSERT_TRUE(legacy_icon.has_value());
    Source.AddComponent<MeshComponent>(light, {*legacy_icon});

    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));

    EntityID loaded = INVALID_ENTITY;
    Candidate->World().ForEach<UUIDComponent>([&](EntityID entity, const UUIDComponent& identity) {
        if (identity.Value == Identity(1))
        {
            loaded = entity;
        }
    });
    ASSERT_TRUE(Candidate->World().IsAlive(loaded));
    EXPECT_TRUE(Candidate->World().HasComponent<LightComponent>(loaded));
    EXPECT_FALSE(Candidate->World().HasComponent<MeshComponent>(loaded));
    EXPECT_EQ(Candidate->Render().Instances.size(), 0u);
    EXPECT_EQ(ResolveCalls, 0u);
}

TEST_F(EditorSceneCandidateTest, LoadedAuthoredGridInitializesTheCompleteOrientedRuntimeSnapshot)
{
    Settings.Grid.Enabled        = false;
    Settings.Grid.BaseSpacing    = .3f;
    Settings.Grid.Origin         = {2.f, 8.f, 4.f};
    Settings.Grid.Orientation    = {.5f, .5f, .5f, .5f};
    Settings.Grid.MajorLineEvery = 7;
    Settings.Grid.Labels         = SceneGridLabels::UV;
    Settings.Grid.FadeRadius     = 1500.f;
    Settings.Grid.FadeStrength   = 1.5f;
    Settings.Grid.LineWidth      = 3.5f;
    Settings.Grid.MaxLOD         = 9;
    Settings.Grid.MinorColor     = {.1f, .2f, .3f, .4f};
    Settings.Grid.MajorColor     = {.2f, .3f, .4f, .5f};
    Settings.Grid.UColor         = {.3f, .4f, .5f, .6f};
    Settings.Grid.VColor         = {.4f, .5f, .6f, .7f};
    SceneDocument document       = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    const auto& render = Candidate->Render();
    EXPECT_TRUE(EqualSceneGridSettings(render.AuthoredGrid, Settings.Grid));
    EXPECT_FALSE(render.Grid.Enabled);
    EXPECT_FLOAT_EQ(render.Grid.CellSize, .3f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.x, 2.f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.y, 8.f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.z, 4.f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.x, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.y, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.z, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.w, .5f);
    EXPECT_EQ(render.Grid.MajorLineEvery, 7u);
    EXPECT_EQ(render.Grid.Labels, uint32_t(SceneGridLabels::UV));
    EXPECT_FLOAT_EQ(render.Grid.FadeRadius, 1500.f);
    EXPECT_FLOAT_EQ(render.Grid.FadeStrength, 1.5f);
    EXPECT_FLOAT_EQ(render.Grid.LineWidth, 3.5f);
    EXPECT_EQ(render.Grid.MaxLOD, 9);
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_FLOAT_EQ(render.Grid.ColorThin[i], Settings.Grid.MinorColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorThick[i], Settings.Grid.MajorColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorUAxis[i], Settings.Grid.UColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorVAxis[i], Settings.Grid.VColor[i]);
    }
    EXPECT_FALSE(Candidate->Render().IsDirty());
}

TEST_F(EditorSceneCandidateTest, MeshDrawCountRejectsScannerSlotZeroAliasesAndIncompleteRanges)
{
    ZEngine::Managers::AssetManager   manager  = {};
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    manager.Registry                                                = &registry;
    // This synthetic cache bypasses ingestion; supply its matching ownership
    // marker as well as the manually constructed descriptor below.
    ZEngine::Managers::AssetManager::AssetPayloadAllocation payload = {};
    manager.MeshCacheAllocations.init(&Memory.MainArena, 1);
    manager.MeshCacheAllocations.push(&payload);
    manager.Meshes.init(&Memory.MainArena, 1);
    auto& mesh    = manager.Meshes.push_use({});
    mesh.MeshUUID = Identity(100);
    mesh.Vertices.init(&Memory.MainArena, 24, 24);
    mesh.Indices.init(&Memory.MainArena, 3, 3);
    mesh.SubMeshes.init(&Memory.MainArena, 1);
    mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3});
    registry.RegisterLoaded(mesh.MeshUUID, ZEngine::Managers::AssetType::MESH, {}, {}, ZEngine::Managers::AssetManager::CreateHandle(0, ZEngine::Managers::AssetType::MESH));
    registry.Register(Identity(101), ZEngine::Managers::AssetType::MESH, {}, {});
    uint32_t draws = 99;
    EXPECT_EQ(manager.GetMeshAsset(Identity(101)), nullptr);
    EXPECT_FALSE(manager.TryGetMeshDrawCount(Identity(101), &draws));
    EXPECT_EQ(draws, 99u);
    EXPECT_FALSE(manager.TryGetMeshDrawCount({}, &draws));
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, nullptr));
    ASSERT_TRUE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    EXPECT_EQ(draws, 1u);
    manager.Meshes[0].SubMeshes[0].IndexOffset = UINT32_MAX;
    draws                                      = 99;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    EXPECT_EQ(draws, 99u);
    manager.Meshes[0].SubMeshes[0].IndexOffset = 0;
    manager.Meshes[0].SubMeshes[0].VertexCount = 4;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    manager.Meshes[0].SubMeshes[0].VertexCount = 3;
    manager.Meshes[0].Vertices.m_size          = 23;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
}

TEST_F(EditorSceneCandidateTest, RebuildsRuntimeBindingsAndDerivedHierarchyWithoutActorDuplication)
{
    const auto parent = AddEntity(1);
    const auto child  = AddEntity(2, true);
    Source.AddComponent<ParentComponent>(child, {parent});
    NameComponent name = {};
    ZEngine::Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Placed mesh", 11);
    Source.AddComponent<NameComponent>(child, name);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->World().AliveCount(), 2u);
    EXPECT_EQ(Candidate->Actors().Count(), 0u);
    ASSERT_EQ(Candidate->Render().Instances.size(), 1u);
    const auto& instance = Candidate->Render().Instances[0];
    EXPECT_STREQ(instance.Name, "Placed mesh");
    EXPECT_EQ(instance.Transform(0, 3), 3.f);
    EXPECT_EQ(instance.Transform(1, 3), 4.f);
    EXPECT_EQ(instance.MeshUUID, Identity(500000));
    Candidate->World().ForEach<MeshComponent>([&](EntityID, const MeshComponent& mesh) {
        EXPECT_EQ(mesh.RenderInstanceId, instance.Id);
        EXPECT_NE(mesh.RenderInstanceId, 12345u);
    });
    EXPECT_EQ(ResolveCalls, 1u);
    EXPECT_EQ(Candidate->DrawCount(), 1u);
    EXPECT_EQ(Source.GetComponent<MeshComponent>(child)->RenderInstanceId, 12345u);
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, FutureActorsAreBoundToTheCandidateWorld)
{
    AddEntity(1);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    const auto handle = Candidate->Actors().Create();
    ASSERT_TRUE(handle.Valid());
    ASSERT_NE(Candidate->Actors().Access(handle), nullptr);
    EXPECT_EQ(Candidate->World().AliveCount(), 2u);
    EXPECT_EQ(Source.AliveCount(), 1u);
    Candidate->Actors().Destroy(handle);
    EXPECT_EQ(Candidate->World().AliveCount(), 1u);
}

TEST_F(EditorSceneCandidateTest, ResolvesEachUniqueAssetOnceAndCountsEveryPlacedSubmesh)
{
    for (uint32_t i = 1; i <= 100; ++i)
    {
        AddEntity(i, true);
    }
    Draws                  = 3;
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->DrawCount(), 300u);
    EXPECT_EQ(Candidate->Render().Instances.size(), 100u);
    EXPECT_EQ(ResolveCalls, 1u);
    EXPECT_GE(Candidate->Render().Instances.capacity(), 100u);
}

TEST_F(EditorSceneCandidateTest, MissingAssetPreservesInputAndReleasesTheFailedCandidateSlot)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    AssetExists = false;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate, nullptr);
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_EQ(document.EntityCount(), 1u);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->Scene, &Source);
    EXPECT_EQ(Source.AliveCount(), 1u);
    EXPECT_TRUE(Diagnostics.HasErrors());
    AssetExists = true;
    EXPECT_TRUE(Prepare(std::move(document)));
}

TEST_F(EditorSceneCandidateTest, ResolverFailurePreservesInputAndReleasesStorage)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    FailWithDiagnostic = true;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_NE(document.Settings(), nullptr);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, RejectsMissingResolverAndZeroDrawBinding)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, {}, &Candidate, &Diagnostics));
    EXPECT_FALSE(AnySlotBusy());
    Draws = 0;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsTransformlessMeshInsteadOfSilentlyDroppingIt)
{
    const auto entity = AddEntity(1, true);
    Source.RemoveComponent<TransformComponent>(entity);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsCompleteInstanceSetBeforeResolvingAssets)
{
    AddEntity(1, true);
    AddEntity(2, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document), {.MaxInstances = 1}));
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Source.AliveCount(), 2u);
}

TEST_F(EditorSceneCandidateTest, RejectsSubmeshBudgetAndMultiplicationOverflowWithoutTruncation)
{
    AddEntity(1, true);
    AddEntity(2, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Draws = 3;
    EXPECT_FALSE(Prepare(std::move(document), {.MaxDrawCommands = 5}));
    EXPECT_FALSE(AnySlotBusy());
    Draws = UINT32_MAX;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_NE(document.Settings(), nullptr);
}

TEST_F(EditorSceneCandidateTest, AcceptsExactDrawBudgetButCannotRaiseTheGPUCap)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Draws = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    EXPECT_FALSE(Prepare(std::move(document), {.MaxDrawCommands = Draws + 1}));
    EXPECT_FALSE(AnySlotBusy());
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->DrawCount(), Draws);
}

TEST_F(EditorSceneCandidateTest, TwoOwnersCoexistAndAFailedThirdLeavesBothIntact)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto first = std::move(Candidate);
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto* second = Candidate.get();
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate.get(), second);
    EXPECT_EQ(first->World().AliveCount(), 1u);
    EXPECT_EQ(second->World().AliveCount(), 1u);
    EXPECT_TRUE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_TRUE(Engine->EditorSceneLoadArenaBInUse.value.load());
    first.reset();
    EXPECT_TRUE(Prepare(std::move(document)));
    EXPECT_TRUE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RepeatedReplacementReusesBothBoundedSlots)
{
    AddEntity(1, true);
    std::array<size_t, 2> high_water = {};
    for (uint32_t iteration = 0; iteration < 100; ++iteration)
    {
        SceneDocument document = {};
        ASSERT_TRUE(Capture(&document));
        ASSERT_TRUE(Prepare(std::move(document)));
        const size_t slot   = Candidate->Render().LocalArena == &Engine->EditorSceneLoadArenaA ? 0 : 1;
        const auto   offset = Candidate->Render().LocalArena->m_current_offset;
        if (high_water[slot])
        {
            EXPECT_EQ(offset, high_water[slot]);
        }
        high_water[slot] = offset;
        EXPECT_STREQ(Candidate->Render().Name, "Owned candidate name");
    }
    EXPECT_NE(high_water[0], 0u);
    EXPECT_NE(high_water[1], 0u);
    Candidate.reset();
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->EditorSceneLoadArenaA.m_current_offset, Engine->EditorSceneLoadArenaA.m_initial_current_offset);
    EXPECT_EQ(Engine->EditorSceneLoadArenaB.m_current_offset, Engine->EditorSceneLoadArenaB.m_initial_current_offset);
}

TEST_F(EditorSceneCandidateTest, RejectsInvalidInputsBeforeClaimingStorage)
{
    SceneDocument document = {};
    EXPECT_FALSE(Prepare(std::move(document)));
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, {}, nullptr, &Diagnostics));
    const auto invalid = VFSPath::Parse("/cache/hidden.zescene").Value();
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, invalid, {}, &Candidate, &Diagnostics));
    EXPECT_NE(document.Settings(), nullptr);
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsUnavailableAndUndersizedSlotsWithoutCrashing)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Engine->EditorSceneLoadArenaA.Shutdown();
    Engine->EditorSceneLoadArenaB.Shutdown();
    EXPECT_FALSE(Prepare(std::move(document)));
    Memory.MainArena.CreateSubArena(ZMega(1), &Engine->EditorSceneLoadArenaA, "CandidateTest/Small");
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, InitializedOwnerCannotClaimASecondSlot)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto* original = Candidate->Render().LocalArena;
    EXPECT_FALSE(Candidate->Render().InitializeDeserialized(*Engine));
    EXPECT_EQ(Candidate->Render().LocalArena, original);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}

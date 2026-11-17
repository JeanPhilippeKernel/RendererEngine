#include <ZEngine/Applications/StandaloneRuntimeApplication.h>
#include <ZEngine/ECS/SceneRuntimeCamera.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <thread>

namespace ZEngine::Applications
{
    namespace
    {
        constexpr size_t CpuBytes      = ZMega(1024ULL);
        constexpr size_t GpuBytes      = ZMega(256ULL);
        constexpr size_t MetadataBytes = ZMega(16ULL);
        constexpr size_t ObjectBytes   = ZMega(512ULL);
        uint64_t         TimestampNs()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    } // namespace

    void StandaloneRuntimeApplication::Configure(std::string directory, uuids::uuid scene, uuids::uuid camera, bool exit_after_frame, uint32_t expected_draws, uint32_t reloads, uuids::uuid alternate, bool retry_failure_probe, bool cancel_upload_probe, bool cancel_cpu_probe, bool supersede_cpu_probe, bool supersede_gpu_probe, bool supersede_waiting_probe)
    {
        m_directory               = std::move(directory);
        m_scene                   = scene;
        m_initial_scene           = scene;
        m_alternate_scene         = alternate;
        m_reloads_remaining       = reloads;
        m_retry_failure_probe     = retry_failure_probe;
        m_cancel_upload_probe     = cancel_upload_probe;
        m_cancel_cpu_probe        = cancel_cpu_probe;
        m_supersede_cpu_probe     = supersede_cpu_probe;
        m_supersede_gpu_probe     = supersede_gpu_probe;
        m_supersede_waiting_probe = supersede_waiting_probe;
        m_camera                  = camera;
        m_exit_after_frame        = exit_after_frame;
        m_expected_draws          = expected_draws;
        EnableRenderOverlay       = true;
    }

    bool StandaloneRuntimeApplication::BeginCapture(void* context, uint32_t width, uint32_t height, uint64_t generation)
    {
        auto& app = *static_cast<StandaloneRuntimeApplication*>(context);
        if (generation != app.m_capture_generation || width != app.m_capture_width || height != app.m_capture_height)
        {
            uint32_t requested = 1;
            app.m_capture_state.compare_exchange_strong(requested, 4, std::memory_order_acq_rel);
            return false;
        }
        uint32_t requested = 1;
        return app.m_capture_state.compare_exchange_strong(requested, 2, std::memory_order_acq_rel);
    }

    void StandaloneRuntimeApplication::CompleteCapture(const void* data, size_t bytes, void* context)
    {
        auto& app = *static_cast<StandaloneRuntimeApplication*>(context);
        if (!data || bytes != app.m_capture_bytes || !app.m_capture_pixels)
        {
            app.m_capture_state.store(4, std::memory_order_release);
            return;
        }
        std::memcpy(app.m_capture_pixels.get(), data, bytes);
        app.m_capture_state.store(3, std::memory_order_release);
    }

    void StandaloneRuntimeApplication::CancelCapture(void* context)
    {
        static_cast<StandaloneRuntimeApplication*>(context)->m_capture_state.store(4, std::memory_order_release);
    }

    void StandaloneRuntimeApplication::OnInitializing()
    {
        Memory->CreateBudgetedArena(Memory->Budget.EditorContext, &m_arena);
    }

    void StandaloneRuntimeApplication::OverrideWindowConfiguration()
    {
        WindowCfg.Title.init(&m_arena, "Standalone runtime");
        WindowCfg.EnableVsync = true;
        if (m_lifecycle_stress || m_fixed_test_window)
        {
            WindowCfg.Width     = 1280;
            WindowCfg.Height    = 720;
            WindowCfg.Resizable = false;
            WindowCfg.Maximize  = false;
        }
    }

    void StandaloneRuntimeApplication::Fail(const char* message, ECS::SceneLoadStage stage)
    {
        if (!m_failed)
            ZENGINE_CORE_ERROR("[CookedRuntime] {}", message)
        RestoreStagingLimit(m_gpu.Status() == Rendering::Scenes::CookedSceneResourceRequest::State::Failed);
        auto* context = Engine::GetContext();
        if (context && context->Device && context->Device->IsDeviceLost.load(std::memory_order_acquire))
        {
            m_failed = true;
            ZENGINE_CORE_ERROR("[CookedRuntime] Device lost; entering fatal shutdown")
            Engine::RequestClose();
            return;
        }
        if (m_host.Active() && m_request)
        {
            auto* engine = Engine::GetContext();
            m_gpu.Cancel();
            const auto status = m_gpu.Tick();
            ZENGINE_VALIDATE_ASSERT(status != Rendering::Scenes::CookedSceneResourceRequest::State::Cancelling && status != Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending, "Runtime failed request still has GPU work")
            m_candidate.reset();
            m_gpu.ReleaseTerminalAssets();
            const bool reset = m_gpu.Reset();
            ZENGINE_VALIDATE_ASSERT(reset, "Runtime failed request did not reset")
            m_assets.Reset();
            m_root.Reset();
            m_storage.Reset();
            if (engine->SceneLoads.Pending(m_request))
                engine->SceneLoads.Fail(m_request, stage, ECS::SceneLoadError::Resources);
            const bool cleaned      = engine->SceneLoads.FinishCleanup(m_request);
            const bool acknowledged = m_request_retain_result || engine->SceneLoads.Acknowledge(m_request);
            ZENGINE_VALIDATE_ASSERT(cleaned && acknowledged, "Runtime failed request cleanup failed")
            if (m_probe_old == m_request)
            {
                ECS::SceneLoadResult outcome = {};
                if (!PollSceneRequest(m_probe_old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Failed || outcome.Stage != stage || outcome.CleanupPending || outcome.Error != ECS::SceneLoadError::Resources || outcome.ExpectedGeneration != engine->ScenePublication.Generation() || outcome.CommittedGeneration != 0 || CancelSceneRequest(m_probe_old) != ECS::SceneCancelResult::AlreadyTerminal || !AcknowledgeSceneRequest(m_probe_old) || PollSceneRequest(m_probe_old, &outcome))
                {
                    m_failed = true;
                    ZENGINE_CORE_ERROR("[CookedRuntime] Failed replacement outcome was not retained correctly")
                    Engine::RequestClose();
                    return;
                }
                m_probe_old = {};
                ZENGINE_CORE_INFO("[CookedRuntime] Failed replacement outcome retained and acknowledged")
            }
            m_request               = {};
            m_request_retain_result = false;
            ZENGINE_CORE_INFO("[CookedRuntime] Replacement failed; active scene generation {} retained", engine->ScenePublication.Generation())
            if (m_retry_failure_probe)
            {
                Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
                m_failure_submission_baseline                                  = RenderPipeline->SceneSubmissions().TryRead(&submitted) && submitted.Generation == engine->ScenePublication.Generation() ? submitted.Count : 0;
                m_waiting_failure_frame                                        = true;
            }
            return;
        }
        m_failed = true;
        Engine::RequestClose();
    }

    void StandaloneRuntimeApplication::RestoreStagingLimit(bool request_failed) noexcept
    {
        if (!m_staging_limit_active)
            return;
        auto*      engine          = Engine::GetContext();
        auto&      allocator       = engine->Device->GpuMem;
        const auto stats           = allocator.GetAllocationStatistics();
        const auto domain          = static_cast<uint32_t>(Core::Memory::GpuMemoryDomain::HostStaging);
        m_staging_refusal_observed = request_failed && m_staging_gpu_started && stats.DomainBudgetRefusals[domain] > m_staging_refusal_baseline;
        const bool restored        = allocator.SetAllocationLimits(m_saved_gpu_limits);
        ZENGINE_VALIDATE_ASSERT(restored, "Runtime test staging limit could not be restored")
        (void) restored;
        m_staging_limit_active = false;
        if (m_staging_refusal_observed)
            ZENGINE_CORE_INFO("[CookedRuntime] Replacement staging allocation was refused")
    }

    void StandaloneRuntimeApplication::OnInitialized()
    {
        auto* engine = Engine::GetContext();
        if (m_source_audit && (!engine || !engine->VFS || !engine->ImportCoordinator || !Managers::AssetManager::Instance() || !Managers::AssetManager::Instance()->Registry))
            return Fail("cannot activate source-independence audit");
        if (!engine || m_directory.empty() || m_scene.is_nil() || !engine->SceneSchemas || !engine->SceneDocumentWorkingBytes || engine->SceneDocumentWorkingBytes > std::numeric_limits<size_t>::max() - ECS::SceneStorageLease::MinimumBytes || !engine->SceneSchemas->FreezeForWorker(&m_scene_diagnostics) || m_cpu_allocator.Initialize(CpuBytes).Failed() || m_gpu_allocator.Initialize(GpuBytes).Failed() || !m_metadata.TryInit(&m_arena, MetadataBytes))
            return Fail("cannot initialize bounded runtime services");
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 8192, .MaxObjectBytes = ObjectBytes, .MaxMeshWorkspaceBytes = ObjectBytes};
        if (!m_cache.Initialize(m_cpu_route, *engine->SceneSchemas, limits, 2, 32, 8192))
            return Fail("cannot initialize cooked closure cache");
        m_cache_initialized = true;
        m_backend.Initialize(m_directory.c_str(), Core::VFS::VFSBackendCaps::Read | Core::VFS::VFSBackendCaps::List, &m_arena);
        m_vfs.Initialize(&m_arena);
        m_vfs_initialized = true;
        if (m_vfs.Mount(&m_backend, Core::VFS::VFSPath::Root(), 0).Failed() || m_vfs.AcquireReadRoot(Core::VFS::VFSPath::Root(), &m_root).Failed() || engine->AdmitSceneStorage(&m_storage, engine->SceneDocumentWorkingBytes + ECS::SceneStorageLease::MinimumBytes) != ECS::SceneStorageAdmission::Acquired)
            return Fail("cannot admit cooked package or scene storage");
        m_generation = engine->ScenePublication.Generation();
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.BeforeInitialAdmission");
        }
        m_admission_ns = TimestampNs();
        if (!engine->SceneLoads.Admit(m_generation, &m_request) || !engine->SceneLoads.TryStart(m_request))
            return Fail("cannot admit cooked scene request");
        m_width       = CurrentWindow ? CurrentWindow->GetWidth() : 0;
        m_height      = CurrentWindow ? CurrentWindow->GetHeight() : 0;
        m_initialized = true;
    }

    void StandaloneRuntimeApplication::RunWorker(void* context)
    {
        auto& self   = *static_cast<StandaloneRuntimeApplication*>(context);
        auto* engine = Engine::GetContext();
        if (self.m_cpu_pause_armed)
        {
            self.m_cpu_boundary.store(true, std::memory_order_release);
            while (!self.m_cancelled.value.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
        while (self.m_cache.PumpOne(&self.m_cancelled.value))
        {
        }
        const Cook::CookLimits limits = {.MaxWorkingBytes = CpuBytes};
        self.m_worker_result          = ReconstructCookedRuntimeScene(std::move(self.m_root), self.m_scene, std::move(self.m_storage), *engine->SceneSchemas, limits, self.m_cpu_allocator, self.m_cpu_route, ObjectBytes, engine->SceneDocumentWorkingBytes, self.m_cache, &self.m_candidate, &self.m_assets, &self.m_cook_diagnostics, &self.m_scene_diagnostics, &self.m_cancelled.value);
        ZENGINE_CORE_INFO("[CookedRuntime] Worker finished with status {}", static_cast<unsigned>(self.m_worker_result))
        self.m_worker_done.value.store(true, std::memory_order_release);
    }

    void StandaloneRuntimeApplication::DrainCacheWorker(void* context)
    {
        auto& self = *static_cast<StandaloneRuntimeApplication*>(context);
        while (self.m_cache.PumpOne())
        {
        }
        self.m_cache_drain_running.value.store(false, std::memory_order_release);
    }

    bool StandaloneRuntimeApplication::RequestScene(const uuids::uuid& scene, ECS::SceneRequestId* output)
    {
        auto* engine = Engine::GetContext();
        if (!m_initialized || m_failed || !engine || !m_host.Active() || scene.is_nil() || engine->CloseRequested.value.load(std::memory_order_acquire))
            return false;
        const uint64_t      generation            = engine->ScenePublication.Generation();
        const uint64_t      admission_ns          = TimestampNs();
        const bool          preparing_was_pending = m_request && engine->SceneLoads.Pending(m_request);
        ECS::SceneRequestId request               = {};
        if (!engine->SceneLoads.Admit(generation, &request))
            return false;
        if (m_waiting_request)
        {
            ECS::SceneLoadResult previous     = {};
            const bool           superseded   = engine->SceneLoads.Poll(m_waiting_request, &previous) && previous.Outcome == ECS::SceneLoadOutcome::Superseded && !previous.CleanupPending;
            const bool           acknowledged = m_waiting_retain_result || engine->SceneLoads.Acknowledge(m_waiting_request);
            ZENGINE_VALIDATE_ASSERT(superseded && acknowledged, "Superseded waiting request lost its terminal outcome")
            m_waiting_superseded_observed = superseded && acknowledged;
        }
        m_waiting_request       = request;
        m_waiting_retain_result = output != nullptr;
        m_waiting_scene         = scene;
        m_waiting_generation    = generation;
        m_waiting_admission_ns  = admission_ns;
        if (m_request)
        {
            if (preparing_was_pending)
                m_request_terminal = ECS::SceneLoadOutcome::Superseded;
            m_cancelled.value.store(true, std::memory_order_release);
            m_gpu.Cancel();
        }
        if (output)
            *output = request;
        return true;
    }

    void StandaloneRuntimeApplication::StartWaiting()
    {
        auto* engine = Engine::GetContext();
        if (!engine || !m_waiting_request || m_request || m_candidate)
            return;
        if (!engine->SceneLoads.Pending(m_waiting_request) || m_waiting_generation != engine->ScenePublication.Generation())
        {
            if (engine->SceneLoads.Pending(m_waiting_request))
                (void) engine->SceneLoads.Cancel(m_waiting_request);
            if (!m_waiting_retain_result)
                (void) engine->SceneLoads.Acknowledge(m_waiting_request);
            m_waiting_request       = {};
            m_waiting_retain_result = false;
            return;
        }
        ECS::SceneStorageLease      storage   = {};
        Core::VFS::VFSReadRootLease root      = {};
        const auto                  admission = engine->AdmitSceneStorage(&storage, engine->SceneDocumentWorkingBytes + ECS::SceneStorageLease::MinimumBytes);
        if (admission == ECS::SceneStorageAdmission::Busy)
            return;
        if (admission != ECS::SceneStorageAdmission::Acquired || m_vfs.AcquireReadRoot(Core::VFS::VFSPath::Root(), &root).Failed())
        {
            (void) engine->SceneLoads.Fail(m_waiting_request, ECS::SceneLoadStage::Admission, ECS::SceneLoadError::Resources);
            if (!m_waiting_retain_result)
                (void) engine->SceneLoads.Acknowledge(m_waiting_request);
            m_waiting_request       = {};
            m_waiting_retain_result = false;
            ZENGINE_CORE_ERROR("[CookedRuntime] Waiting replacement admission failed; active scene retained")
            return;
        }
        if (!engine->SceneLoads.TryStart(m_waiting_request))
            return;
        m_scene                 = m_waiting_scene;
        m_generation            = m_waiting_generation;
        m_admission_ns          = m_waiting_admission_ns;
        m_request               = m_waiting_request;
        m_request_retain_result = m_waiting_retain_result;
        m_request_terminal      = ECS::SceneLoadOutcome::Pending;
        m_waiting_request       = {};
        m_waiting_retain_result = false;
        m_storage               = std::move(storage);
        m_root                  = std::move(root);
        m_cook_diagnostics.Clear();
        m_scene_diagnostics.Clear();
        m_worker_result = CookedRuntimeReconstructionError::InvalidInput;
        m_worker_done.value.store(false, std::memory_order_relaxed);
        m_cpu_boundary.store(false, std::memory_order_relaxed);
        m_cancelled.value.store(false, std::memory_order_release);
        m_cpu_pause_armed         = m_cancel_cpu_probe || m_supersede_cpu_probe || m_shutdown_probe_cpu;
        m_cpu_supersede_armed     = m_supersede_cpu_probe;
        m_cancel_cpu_probe        = false;
        m_supersede_cpu_probe     = false;
        m_dispatched              = false;
        m_gpu_started             = false;
        m_last_gpu_status         = 255;
        m_publication_wait_logged = false;
    }

    ECS::SceneCancelResult StandaloneRuntimeApplication::CancelSceneRequest() noexcept
    {
        auto* engine = Engine::GetContext();
        if (!engine || !m_host.Active())
            return ECS::SceneCancelResult::NotFound;
        return CancelSceneRequest(m_waiting_request ? m_waiting_request : m_request);
    }

    bool StandaloneRuntimeApplication::PollSceneRequest(ECS::SceneRequestId request, ECS::SceneLoadResult* output) const noexcept
    {
        const auto* engine = Engine::GetContext();
        return engine && engine->SceneLoads.Poll(request, output);
    }

    bool StandaloneRuntimeApplication::AcknowledgeSceneRequest(ECS::SceneRequestId request) noexcept
    {
        auto* engine = Engine::GetContext();
        return engine && engine->SceneLoads.Acknowledge(request);
    }

    ECS::SceneCancelResult StandaloneRuntimeApplication::CancelSceneRequest(ECS::SceneRequestId request) noexcept
    {
        auto* engine = Engine::GetContext();
        if (!engine || !request || !m_host.Active())
            return ECS::SceneCancelResult::NotFound;
        const uint64_t cancellation_ns = m_lifecycle_stress && request == m_request ? TimestampNs() : 0;
        const auto     result          = engine->SceneLoads.Cancel(request);
        if (result != ECS::SceneCancelResult::Cancelled)
            return result;
        if (request == m_waiting_request)
        {
            if (!m_waiting_retain_result)
                (void) engine->SceneLoads.Acknowledge(request);
            m_waiting_request       = {};
            m_waiting_retain_result = false;
        }
        else if (request == m_request)
        {
            if (m_lifecycle_stress)
                m_stress_cancel_requested_ns = cancellation_ns;
            m_request_terminal = ECS::SceneLoadOutcome::Cancelled;
            m_cancelled.value.store(true, std::memory_order_release);
            m_gpu.Cancel();
        }
        return result;
    }

    void StandaloneRuntimeApplication::OnUpdate(float)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.RuntimeUpdate");
        if (!m_initialized || m_failed)
            return;
        auto* engine = Engine::GetContext();
        if (engine->Device && engine->Device->IsDeviceLost.load(std::memory_order_acquire))
            return Fail("device lost during runtime scene work");
        if (m_observe_gpu_completion && engine->Device && engine->Device->SwapchainPtr && engine->Device->SwapchainPtr->RenderTimeline)
        {
            Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
            if (RenderPipeline->SceneSubmissions().TryRead(&submitted) && submitted.Generation == engine->ScenePublication.Generation() && submitted.Generation != m_completed_generation && submitted.Count)
            {
                if (!submitted.FirstTimelineValue)
                    return Fail("scene submission did not provide its render timeline");
                uint64_t       completed = 0;
                const VkResult status    = vkGetSemaphoreCounterValue(engine->Device->LogicalDevice, engine->Device->SwapchainPtr->RenderTimeline->GetHandle(), &completed);
                if (status != VK_SUCCESS)
                {
                    if (status == VK_ERROR_DEVICE_LOST)
                        (void) engine->Device->CheckDeviceLost(status, "runtime completion observation");
                    return Fail("cannot observe scene render completion");
                }
                if (completed >= submitted.FirstTimelineValue)
                {
                    m_completed_generation   = submitted.Generation;
                    m_completion_observed_ns = TimestampNs();
                }
            }
        }
        if (m_request && m_cpu_pause_armed && !m_cpu_cancellation_observed && m_cpu_boundary.load(std::memory_order_acquire))
        {
            if (m_shutdown_probe_cpu && m_host.Active())
            {
                m_shutdown_probe_started    = true;
                m_shutdown_probe_generation = engine->ScenePublication.Generation();
                m_frame_recorded            = true;
                ZENGINE_CORE_INFO("[CookedRuntime] Shutdown requested with CPU worker pending")
                Engine::RequestClose();
                return;
            }
            if (m_cpu_supersede_armed)
            {
                const auto           old     = m_request;
                ECS::SceneLoadResult outcome = {};
                if (!RequestScene(m_initial_scene) || !engine->SceneLoads.Poll(old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Superseded || !outcome.CleanupPending)
                    return Fail("cannot supersede runtime CPU worker");
                if (m_probe_old == old)
                {
                    if (!AcknowledgeSceneRequest(old) || PollSceneRequest(old, &outcome))
                        return Fail("cannot acknowledge superseded worker outcome");
                    m_probe_old = {};
                    ZENGINE_CORE_INFO("[CookedRuntime] Superseded worker outcome acknowledged before cleanup")
                }
                m_cpu_supersede_armed       = false;
                m_cpu_cancellation_observed = true;
                ZENGINE_CORE_INFO("[CookedRuntime] Superseded CPU worker with cleanup pending")
                return;
            }
            if (CancelSceneRequest() != ECS::SceneCancelResult::Cancelled)
                return Fail("cannot cancel runtime CPU worker");
            if (m_probe_old == m_request)
            {
                ECS::SceneLoadResult outcome = {};
                if (!PollSceneRequest(m_probe_old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Cancelled || !outcome.CleanupPending || !AcknowledgeSceneRequest(m_probe_old) || PollSceneRequest(m_probe_old, &outcome))
                    return Fail("cannot acknowledge cancelled worker outcome");
                m_probe_old = {};
                ZENGINE_CORE_INFO("[CookedRuntime] Cancelled worker outcome acknowledged before cleanup")
            }
            m_cpu_cancellation_observed = true;
            ZENGINE_CORE_INFO("[CookedRuntime] Cancelled replacement while CPU worker owned its inputs")
        }
        if (m_request && !engine->SceneLoads.Pending(m_request))
        {
            const auto outcome = m_request_terminal;
            if (outcome != ECS::SceneLoadOutcome::Cancelled && outcome != ECS::SceneLoadOutcome::Superseded)
                return Fail("lost runtime request outcome");
            m_cancelled.value.store(true, std::memory_order_release);
            if (m_dispatched && !m_worker_done.value.load(std::memory_order_acquire))
                return;
            m_gpu.Cancel();
            const auto state = m_gpu.Tick();
            if (state == Rendering::Scenes::CookedSceneResourceRequest::State::Cancelling || state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending)
                return;
            m_candidate.reset();
            m_gpu.ReleaseTerminalAssets();
            const bool reset = m_gpu.Reset();
            m_assets.Reset();
            m_root.Reset();
            m_storage.Reset();
            const bool cleaned      = engine->SceneLoads.FinishCleanup(m_request);
            const bool acknowledged = m_request_retain_result || engine->SceneLoads.Acknowledge(m_request);
            ZENGINE_VALIDATE_ASSERT(reset && cleaned && acknowledged, "Runtime cancelled request cleanup failed")
            m_request               = {};
            m_request_retain_result = false;
            if (outcome == ECS::SceneLoadOutcome::Superseded)
            {
                ZENGINE_CORE_INFO("[CookedRuntime] Superseded replacement drained; active scene retained")
            }
            else
            {
                ZENGINE_CORE_INFO("[CookedRuntime] Cancelled replacement drained; active scene generation {} retained", engine->ScenePublication.Generation())
            }
            if (m_lifecycle_stress && outcome == ECS::SceneLoadOutcome::Cancelled)
            {
                if (engine->ScenePublication.Generation() != m_stress_cancel_generation)
                    return Fail("stress cancellation changed the active generation");
                Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
                if (!RenderPipeline->SceneSubmissions().TryRead(&submitted) || submitted.Generation != m_stress_cancel_generation)
                    return Fail("stress cancellation lost the active scene submission");
                m_stress_submission_baseline = submitted.Count;
                m_stress_waiting_frame       = true;
                ++m_stress_cancelled;
                const auto cleanup_ns = TimestampNs();
                if (!m_stress_cancel_requested_ns || cleanup_ns < m_stress_cancel_requested_ns)
                    return Fail("invalid stress cancellation timing");
                ZENGINE_CORE_INFO("[CookedCancelTiming] cancelled={} boundary={} request_ns={} cleanup_ns={} elapsed_ns={}", m_stress_cancelled, (m_stress_cancelled % 2) ? "cpu" : "gpu", m_stress_cancel_requested_ns, cleanup_ns, cleanup_ns - m_stress_cancel_requested_ns)
                m_stress_cancel_requested_ns = 0;
            }
            if (m_retry_failure_probe && !m_waiting_request)
            {
                Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
                m_failure_submission_baseline                                  = RenderPipeline->SceneSubmissions().TryRead(&submitted) && submitted.Generation == engine->ScenePublication.Generation() ? submitted.Count : 0;
                m_waiting_failure_frame                                        = true;
            }
            return;
        }
        if (!m_request && m_waiting_request)
        {
            StartWaiting();
            return;
        }
        if (m_request && !m_dispatched)
        {
            if (m_cache_drain_running.value.load(std::memory_order_acquire))
                return;
            auto* pool = Helpers::ThreadPoolHelper::Pool.get();
            if (pool && pool->Submit(this, &RunWorker))
                m_dispatched = true;
            return;
        }
        if (m_request && !m_worker_done.value.load(std::memory_order_acquire))
            return;
        if (m_request && m_worker_result != CookedRuntimeReconstructionError::None)
        {
            if (m_worker_result == CookedRuntimeReconstructionError::Assets)
                ZENGINE_CORE_ERROR("[CookedRuntime] Asset closure reconstruction failed")
            for (uint32_t i = 0; i < m_cook_diagnostics.Count; ++i)
                ZENGINE_CORE_ERROR("[CookedRuntime] {}", m_cook_diagnostics.Entries[i].Message)
            for (uint32_t i = 0; i < m_scene_diagnostics.Count; ++i)
                ZENGINE_CORE_ERROR("[CookedRuntime] {}", m_scene_diagnostics.Entries[i].Message)
            return Fail("cooked reconstruction failed");
        }
        if (m_request)
        {
            if (!m_width || !m_height)
                return;
            if (!m_gpu_started)
            {
                RenderFrameState frame = {};
                frame.RenderTargetW    = m_width;
                frame.RenderTargetH    = m_height;
                frame.ResizeSequence   = m_resize_sequence;
                ECS::Systems::SyncHierarchy(m_candidate->Instance().World());
                if (!ECS::CaptureSceneRuntimeCamera(m_candidate->Instance().World(), m_camera, m_width, m_height, &frame.Camera, &m_scene_diagnostics))
                {
                    for (uint32_t i = 0; i < m_scene_diagnostics.Count; ++i)
                        ZENGINE_CORE_ERROR("[CookedRuntime] {}", m_scene_diagnostics.Entries[i].Message)
                    return Fail("runtime camera is invalid");
                }
                if (!m_host.Active())
                {
                    m_bootstrap.Instances.m_allocator = &m_arena;
                    m_bootstrap.Sky.Mode              = Rendering::Scenes::SkyMode::SkySphere;
                    m_bootstrap_camera                = frame.Camera;
                    CurrentScene                      = &m_bootstrap;
                }
                const Rendering::Scenes::SceneInitialView view = {frame.Camera, m_width, m_height, m_resize_sequence, false, true};
                ZENGINE_CORE_INFO("[CookedRuntime] Candidate sky mode {}", static_cast<unsigned>(m_candidate->Render().Sky.Mode))
                if (m_host.Active() && m_fail_staging_probe && !m_staging_limit_active)
                {
                    auto&      allocator        = engine->Device->GpuMem;
                    const auto stats            = allocator.GetAllocationStatistics();
                    const auto domain           = static_cast<uint32_t>(Core::Memory::GpuMemoryDomain::HostStaging);
                    auto       limited          = allocator.GetAllocationLimits();
                    m_saved_gpu_limits          = limited;
                    limited.DomainBytes[domain] = stats.DomainAdmittedBytes[domain];
                    if (!allocator.SetAllocationLimits(limited))
                        return;
                    m_staging_refusal_baseline = stats.DomainBudgetRefusals[domain];
                    m_staging_limit_active     = true;
                    m_fail_staging_probe       = false;
                }
                if (!m_gpu.Begin(m_candidate->Instance(), m_candidate->Render(), m_candidate->Resources(), m_candidate->RequiredMeshes(), std::move(m_assets), m_request, m_generation, view, m_gpu_route, m_metadata, *engine->RenderResourceManager, *engine->Device, RenderPipeline->CookedSceneUploads, Rendering::RenderResourceManager::SceneCookedTextureCapacity, m_host.Active() && (m_cancel_upload_probe || m_supersede_gpu_probe || m_device_loss_probe || m_shutdown_probe_gpu)))
                    return Fail("cannot begin cooked GPU preparation");
                if (m_host.Active() && m_reject_upload_probe)
                {
                    engine->RenderResourceManager->ArmCookedBatchRejectionProbe();
                    m_reject_upload_probe = false;
                }
                m_gpu_started = true;
                // Camera selection and resource admission both scale with the
                // candidate. Give each its own update rather than charging both
                // to the same main-thread frame at the scene-size limit.
                return;
            }
            const auto state = m_gpu.Tick();
            if (m_shutdown_probe_gpu && m_host.Active() && state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending && m_gpu.MeshUploadRecorded())
            {
                m_shutdown_probe_started    = true;
                m_shutdown_probe_generation = engine->ScenePublication.Generation();
                m_frame_recorded            = true;
                ZENGINE_CORE_INFO("[CookedRuntime] Shutdown requested with GPU upload pending")
                Engine::RequestClose();
                return;
            }
            if (m_staging_limit_active && state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending)
                m_staging_gpu_started = true;
            if (m_device_loss_probe && m_host.Active() && state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending && m_gpu.MeshUploadRecorded())
            {
                m_device_loss_probe = false;
                ZENGINE_CORE_INFO("[CookedRuntime] Injecting device loss after upload recording")
                (void) engine->Device->CheckDeviceLost(VK_ERROR_DEVICE_LOST, "injected runtime upload loss");
                return Fail("device lost during runtime scene work");
            }
            if (m_supersede_gpu_probe && m_host.Active() && state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending && m_gpu.MeshUploadRecorded())
            {
                const auto           old     = m_request;
                ECS::SceneLoadResult outcome = {};
                if (!RequestScene(m_initial_scene) || !engine->SceneLoads.Poll(old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Superseded || !outcome.CleanupPending)
                    return Fail("cannot supersede recorded runtime upload");
                m_supersede_gpu_probe = false;
                ZENGINE_CORE_INFO("[CookedRuntime] Superseded recorded upload with cleanup pending")
                return;
            }
            if (m_cancel_upload_probe && m_host.Active() && state == Rendering::Scenes::CookedSceneResourceRequest::State::GpuPending && m_gpu.MeshUploadRecorded())
            {
                if (CancelSceneRequest() != ECS::SceneCancelResult::Cancelled)
                    return Fail("cannot cancel recorded runtime upload");
                if (m_probe_old == m_request)
                {
                    ECS::SceneLoadResult outcome = {};
                    if (!PollSceneRequest(m_probe_old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Cancelled || !outcome.CleanupPending || !AcknowledgeSceneRequest(m_probe_old) || PollSceneRequest(m_probe_old, &outcome))
                        return Fail("cannot acknowledge cancelled upload outcome");
                    m_probe_old = {};
                    ZENGINE_CORE_INFO("[CookedRuntime] Cancelled upload outcome acknowledged before cleanup")
                }
                m_cancel_upload_probe          = false;
                m_upload_cancellation_observed = true;
                ZENGINE_CORE_INFO("[CookedRuntime] Cancelled replacement after mesh upload recording")
                return;
            }
            if (m_last_gpu_status != static_cast<uint8_t>(state))
            {
                m_last_gpu_status = static_cast<uint8_t>(state);
                ZENGINE_CORE_INFO("[CookedRuntime] GPU preparation state {}", static_cast<unsigned>(state))
            }
            if (state == Rendering::Scenes::CookedSceneResourceRequest::State::Failed || state == Rendering::Scenes::CookedSceneResourceRequest::State::Cancelled)
                return Fail("cooked GPU preparation failed");
            if (state != Rendering::Scenes::CookedSceneResourceRequest::State::Ready)
                return;
            if (m_host.Active() && m_fail_preflight_probe)
            {
                m_fail_preflight_probe = false;
                return Fail("injected publication preflight refusal", ECS::SceneLoadStage::Publication);
            }
            RenderFrameState frame = {};
            frame.RenderTargetW    = m_width;
            frame.RenderTargetH    = m_height;
            frame.ResizeSequence   = m_resize_sequence;
            if (!m_host.Publish(*engine, *this, m_candidate, m_request, m_generation, m_camera, frame, &m_scene_diagnostics))
            {
                if (!m_publication_wait_logged)
                    ZENGINE_CORE_WARN("[CookedRuntime] Waiting for scene publication preflight")
                m_publication_wait_logged = true;
                return;
            }
            if (!m_gpu.TakeReady())
                return Fail("cannot finish cooked GPU preparation");
            const bool cleaned      = engine->SceneLoads.FinishCleanup(m_request);
            const bool acknowledged = m_request_retain_result || engine->SceneLoads.Acknowledge(m_request);
            ZENGINE_VALIDATE_ASSERT(cleaned && acknowledged, "Runtime scene request cleanup failed")
            m_request               = {};
            m_request_retain_result = false;
            m_active_scene          = m_scene;
            if (m_lifecycle_stress && m_stress_admitted)
                ++m_stress_committed;
            m_active_admission_ns = m_admission_ns;
            ZENGINE_CORE_INFO("[CookedRuntime] Published cooked scene generation {} scene={}", engine->ScenePublication.Generation(), uuids::to_string(m_active_scene))
            if (m_probe_latest)
            {
                ECS::SceneLoadResult outcome = {};
                if (!PollSceneRequest(m_probe_latest, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Committed || outcome.CommittedGeneration != engine->ScenePublication.Generation() || outcome.CleanupPending || CancelSceneRequest(m_probe_latest) != ECS::SceneCancelResult::TooLate || !AcknowledgeSceneRequest(m_probe_latest) || PollSceneRequest(m_probe_latest, &outcome))
                    return Fail("runtime committed request outcome was not retained correctly");
                m_probe_latest = {};
                ZENGINE_CORE_INFO("[CookedRuntime] Committed request outcome retained and acknowledged")
            }
        }
        if (!m_request && !m_waiting_request && !m_cache_drain_running.value.load(std::memory_order_acquire))
        {
            Managers::CookedCpuClosureCache::Statistics cache = {};
            if (m_cache.TryGetStatistics(&cache) && cache.Retiring)
            {
                auto* pool = Helpers::ThreadPoolHelper::Pool.get();
                m_cache_drain_running.value.store(true, std::memory_order_release);
                if (!pool || !pool->Submit(this, &DrainCacheWorker))
                    m_cache_drain_running.value.store(false, std::memory_order_release);
            }
        }
        if (m_exit_after_frame)
        {
            Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
            if (m_stress_waiting_frame)
            {
                if (!RenderPipeline->SceneSubmissions().TryRead(&submitted) || submitted.Generation != m_stress_cancel_generation || submitted.Count <= m_stress_submission_baseline)
                    return;
                m_stress_waiting_frame = false;
                ZENGINE_CORE_INFO("[CookedStress] cancelled={} generation={} rendered_after_cleanup=1", m_stress_cancelled, submitted.Generation)
            }
            if (m_waiting_failure_frame)
            {
                if (RenderPipeline->SceneSubmissions().TryRead(&submitted) && submitted.Generation == engine->ScenePublication.Generation() && submitted.Count > m_failure_submission_baseline && RequestScene(m_initial_scene))
                {
                    ZENGINE_CORE_INFO("[CookedRuntime] Active scene submitted after failure; retry admitted")
                    m_waiting_failure_frame = false;
                    m_retry_failure_probe   = false;
                }
                return;
            }
            if (RenderPipeline->SceneSubmissions().TryRead(&submitted) && submitted.Generation == engine->ScenePublication.Generation() && submitted.Count >= 8)
            {
                if (m_expected_draws && submitted.DrawCount != m_expected_draws)
                    return Fail("submitted draw count does not match fixture");
                if (m_reported_generation != submitted.Generation)
                {
                    if (m_observe_gpu_completion && m_completed_generation != submitted.Generation)
                        return;
                    Managers::CookedCpuClosureCache::Statistics cache = {};
                    if (!m_cache.TryGetStatistics(&cache))
                        return;
                    if (cache.Retiring || m_cache_drain_running.value.load(std::memory_order_acquire))
                        return;
                    if (!m_active_admission_ns || submitted.FirstTimestampNs < m_active_admission_ns)
                        return Fail("invalid runtime admission-to-submission timing");
                    const auto cpu        = m_cpu_allocator.Statistics();
                    const auto staging    = m_gpu_allocator.Statistics();
                    const auto metadata   = m_metadata.GetStatistics();
                    const auto gpu        = engine->Device->GpuMem.GetAllocationStatistics();
                    const auto retirement = engine->Device->PendingFree.GetStatistics();
                    ZENGINE_CORE_INFO("[CookedTiming] generation={} admission_ns={} first_submission_ns={} elapsed_ns={}", submitted.Generation, m_active_admission_ns, submitted.FirstTimestampNs, submitted.FirstTimestampNs - m_active_admission_ns)
                    if (m_observe_gpu_completion)
                    {
                        if (m_completion_observed_ns < submitted.FirstTimestampNs)
                            return Fail("invalid observed render completion timing");
                        ZENGINE_CORE_INFO("[CookedGpuCompletion] generation={} observed_ns={} timeline_value={}", submitted.Generation, m_completion_observed_ns, submitted.FirstTimelineValue)
                    }
                    ZENGINE_CORE_INFO("[CookedLifecycle] generation={} cpu_live={} cpu_peak={} staging_live={} staging_peak={} metadata_live={} metadata_blocks={} gpu_live={} gpu_peak={} retirement_queued={} retirement_draining={}", submitted.Generation, cpu.LiveBytes, cpu.PeakBytes, staging.LiveBytes, staging.PeakBytes, metadata.LiveBytes, metadata.LiveBlocks, gpu.AllocationBytes, gpu.PeakAllocationBytes, retirement.Queued, retirement.Draining)
                    const auto domain_peak = [&gpu](Core::Memory::GpuMemoryDomain domain) { return gpu.DomainPeakAdmittedBytes[static_cast<uint32_t>(domain)]; };
                    ZENGINE_CORE_INFO(
                        "[CookedGpuBudget] generation={} admitted_peak={} geometry_peak={} texture_peak={} target_peak={} uniform_peak={} staging_peak={} readback_peak={}", submitted.Generation, gpu.PeakAdmittedBytes, domain_peak(Core::Memory::GpuMemoryDomain::DeviceGeometry), domain_peak(Core::Memory::GpuMemoryDomain::DeviceTexture), domain_peak(Core::Memory::GpuMemoryDomain::RenderTarget), domain_peak(Core::Memory::GpuMemoryDomain::HostUniform), domain_peak(Core::Memory::GpuMemoryDomain::HostStaging), domain_peak(Core::Memory::GpuMemoryDomain::HostReadback))
                    ZENGINE_CORE_INFO("[CookedRetirementBudget] generation={} capacity={} high_water={}", submitted.Generation, retirement.Capacity, retirement.HighWater)
                    ZENGINE_CORE_INFO("[CookedCache] generation={} closures={} leases={} retiring={} asset_versions={}", submitted.Generation, cache.Closures, cache.Leases, cache.Retiring, cache.AssetVersions)
                    m_reported_generation = submitted.Generation;
                }
                if (m_steady_frames && !m_steady_complete)
                {
                    if (!m_steady_start_ns || m_steady_generation != submitted.Generation || submitted.Count < m_steady_start_count)
                    {
                        m_steady_start_count = submitted.Count;
                        m_steady_generation  = submitted.Generation;
                        m_steady_start_ns    = TimestampNs();
                    }
                    if (submitted.Count - m_steady_start_count < m_steady_frames)
                        return;
                    const auto end_ns = TimestampNs();
                    ZENGINE_CORE_INFO("[CookedSteady] generation={} start_ns={} end_ns={} submitted_frames={}", submitted.Generation, m_steady_start_ns, end_ns, submitted.Count - m_steady_start_count)
                    m_steady_complete = true;
                }
                if (m_reloads_remaining)
                {
                    const auto next = m_active_scene == m_initial_scene ? m_alternate_scene : m_initial_scene;
                    if (m_lifecycle_stress && (m_stress_admitted % 2 == 0))
                    {
                        m_cancel_cpu_probe          = m_stress_admitted % 4 == 0;
                        m_cancel_upload_probe       = !m_cancel_cpu_probe;
                        m_cpu_cancellation_observed = false;
                        m_stress_cancel_generation  = engine->ScenePublication.Generation();
                    }
                    if (RequestScene(next, (m_supersede_waiting_probe || m_supersede_cpu_probe || m_cancel_cpu_probe || (m_lifecycle_stress && m_cancel_upload_probe) || (m_retry_failure_probe && !m_cancel_upload_probe)) ? &m_probe_old : nullptr))
                    {
                        --m_reloads_remaining;
                        if (m_lifecycle_stress)
                            ++m_stress_admitted;
                        if (m_supersede_waiting_probe)
                        {
                            m_waiting_superseded_observed = false;
                            ECS::SceneLoadResult outcome  = {};
                            if (!RequestScene(m_initial_scene, &m_probe_latest) || !m_waiting_superseded_observed || !PollSceneRequest(m_probe_old, &outcome) || outcome.Outcome != ECS::SceneLoadOutcome::Superseded || outcome.CleanupPending || CancelSceneRequest(m_probe_old) != ECS::SceneCancelResult::AlreadyTerminal || !AcknowledgeSceneRequest(m_probe_old) || PollSceneRequest(m_probe_old, &outcome))
                                return Fail("cannot retain superseded waiting request outcome");
                            m_probe_old               = {};
                            m_supersede_waiting_probe = false;
                            ZENGINE_CORE_INFO("[CookedRuntime] Superseded waiting request before worker admission")
                        }
                    }
                    return;
                }
                if (m_post_steady_frames && !m_post_steady_complete)
                {
                    if (!m_post_steady_start_ns || m_post_steady_generation != submitted.Generation || submitted.Count < m_post_steady_start_count)
                    {
                        m_post_steady_start_count = submitted.Count;
                        m_post_steady_generation  = submitted.Generation;
                        m_post_steady_start_ns    = TimestampNs();
                    }
                    if (submitted.Count - m_post_steady_start_count < m_post_steady_frames)
                        return;
                    const auto end_ns = TimestampNs();
                    ZENGINE_CORE_INFO("[CookedPostSteady] generation={} start_ns={} end_ns={} submitted_frames={}", submitted.Generation, m_post_steady_start_ns, end_ns, submitted.Count - m_post_steady_start_count)
                    m_post_steady_complete = true;
                }
                if (m_capture_path.empty())
                {
                    if (m_lifecycle_stress)
                    {
                        if (m_stress_cancelled + m_stress_committed != m_stress_admitted)
                            return Fail("stress cycle lost a terminal outcome");
                        ZENGINE_CORE_INFO("[CookedStress] admitted={} cancelled={} committed={}", m_stress_admitted, m_stress_cancelled, m_stress_committed)
                    }
                    m_frame_recorded = true;
                    ZENGINE_CORE_INFO("[CookedRuntime] Submitted {} draws", submitted.DrawCount)
                    Engine::RequestClose();
                }
                else
                {
                    const auto capture = m_capture_state.load(std::memory_order_acquire);
                    if (capture == 0)
                    {
                        if (!m_width || !m_height || uint64_t(m_width) * m_height > 32ULL * 1024 * 1024)
                            return Fail("invalid capture extent");
                        m_capture_width      = m_width;
                        m_capture_height     = m_height;
                        m_capture_generation = submitted.Generation;
                        m_capture_bytes      = size_t(m_width) * m_height * 4;
                        m_capture_pixels.reset(new (std::nothrow) uint8_t[m_capture_bytes]);
                        if (!m_capture_pixels)
                            return Fail("cannot allocate capture buffer");
                        m_capture_state.store(1, std::memory_order_release);
                    }
                    else if (capture == 3)
                    {
                        const size_t pixels = size_t(m_capture_width) * m_capture_height;
                        for (size_t i = 0; i < pixels; ++i)
                        {
                            m_capture_pixels[3 * i]     = m_capture_pixels[4 * i];
                            m_capture_pixels[3 * i + 1] = m_capture_pixels[4 * i + 1];
                            m_capture_pixels[3 * i + 2] = m_capture_pixels[4 * i + 2];
                        }
                        std::ofstream output(m_capture_path, std::ios::binary | std::ios::trunc);
                        output << "P6\n" << m_capture_width << ' ' << m_capture_height << "\n255\n";
                        output.write(reinterpret_cast<const char*>(m_capture_pixels.get()), static_cast<std::streamsize>(pixels * 3));
                        output.close();
                        if (output.fail())
                            return Fail("cannot write scene capture");
                        m_frame_recorded = true;
                        ZENGINE_CORE_INFO("[CookedRuntime] Submitted {} draws", submitted.DrawCount)
                        Engine::RequestClose();
                    }
                    else if (capture == 4)
                        return Fail("scene capture failed");
                }
            }
        }
    }

    void StandaloneRuntimeApplication::PrepareScene(RenderFrameState& state)
    {
        state.RenderTargetW  = m_width;
        state.RenderTargetH  = m_height;
        state.ResizeSequence = m_resize_sequence;
        state.ShowGrid       = false;
        state.Gizmo.Reset();
        state.SceneCapture = {};
        if (m_capture_state.load(std::memory_order_acquire) == 1)
            state.SceneCapture = {this, &BeginCapture, &CompleteCapture, &CancelCapture};
        if (!m_host.Active())
            state.Camera = m_bootstrap_camera;
        if (auto* active = m_host.Active())
        {
            if (m_width && m_height && !ECS::CaptureSceneRuntimeCamera(active->Instance().World(), m_camera, m_width, m_height, &state.Camera, &m_scene_diagnostics))
                return Fail("active runtime camera is invalid");
            state.SceneResources = active->Resources().InitialResources();
        }
    }

    void StandaloneRuntimeApplication::OnRenderUI()
    {
        auto* ctx = RenderPipeline ? RenderPipeline->ZUICtx : nullptr;
        if (!ctx || !RenderPipeline->SceneRenderer)
            return;
        if (ctx->ScreenW && ctx->ScreenH && (ctx->ScreenW != m_width || ctx->ScreenH != m_height))
        {
            m_width  = ctx->ScreenW;
            m_height = ctx->ScreenH;
            ++m_resize_sequence;
        }
        const auto texture  = RenderPipeline->SceneRenderer->GetFrameOutput();
        auto*      image    = UI::ZUIBeginColumn(ctx, "##runtime", UI::ZPx(static_cast<float>(ctx->ScreenW)), UI::ZPx(static_cast<float>(ctx->ScreenH)));
        image->Flags        = image->Flags | UI::ZUI_DrawBackground;
        image->TextureIndex = texture.Valid() ? static_cast<uint32_t>(texture.Index) : 0xFFFFFFFFu;
        image->EdgeSoftness = 0.f;
        ZUIBoxSetColor(image, 1.f, 1.f, 1.f, texture.Valid() ? 1.f : 0.f);
        UI::ZUIEndColumn(ctx);
    }

    void StandaloneRuntimeApplication::OnClosing()
    {
        RestoreStagingLimit(false);
        m_cancelled.value.store(true, std::memory_order_release);
        m_gpu.Cancel();
    }

    void StandaloneRuntimeApplication::OnWorldShutdown(EngineContext& engine)
    {
        m_gpu.Cancel();
        (void) m_gpu.Tick();
        m_candidate.reset();
        m_gpu.ReleaseTerminalAssets();
        const bool reset   = m_gpu.Reset();
        const bool retired = m_host.ShutdownAfterReadersJoined(engine, *this);
        ZENGINE_VALIDATE_ASSERT(reset && retired, "Runtime scene shutdown did not drain its owners")
        m_assets.Reset();
        m_root.Reset();
        m_storage.Reset();
        if (m_request)
        {
            if (engine.SceneLoads.Pending(m_request))
                engine.SceneLoads.Cancel(m_request);
            ECS::SceneLoadResult shutdown_result = {};
            const bool           terminal        = !m_shutdown_probe_started || (engine.SceneLoads.Poll(m_request, &shutdown_result) && shutdown_result.Outcome == ECS::SceneLoadOutcome::Cancelled && shutdown_result.CleanupPending && shutdown_result.ExpectedGeneration == m_shutdown_probe_generation && engine.ScenePublication.Generation() == m_shutdown_probe_generation);
            const bool           cleaned         = engine.SceneLoads.FinishCleanup(m_request);
            if (m_shutdown_probe_started)
            {
                m_shutdown_probe_passed = terminal && cleaned && engine.SceneLoads.Poll(m_request, &shutdown_result) && !shutdown_result.CleanupPending && shutdown_result.Outcome == ECS::SceneLoadOutcome::Cancelled;
                if (m_shutdown_probe_passed)
                    ZENGINE_CORE_INFO("[CookedRuntime] Shutdown cancelled and drained pending replacement")
                else
                    ZENGINE_CORE_ERROR("[CookedRuntime] Shutdown replacement outcome or cleanup invalid")
            }
            if (!m_request_retain_result)
                (void) engine.SceneLoads.Acknowledge(m_request);
            m_request               = {};
            m_request_retain_result = false;
        }
        if (m_waiting_request)
        {
            if (engine.SceneLoads.Pending(m_waiting_request))
                (void) engine.SceneLoads.Cancel(m_waiting_request);
            if (!m_waiting_retain_result)
                (void) engine.SceneLoads.Acknowledge(m_waiting_request);
            m_waiting_request       = {};
            m_waiting_retain_result = false;
        }
        if (m_source_audit)
        {
            const auto vfs        = static_cast<Core::VFS::VFSContext*>(engine.VFS)->DeniedSourceAccesses();
            const auto registry   = Managers::AssetManager::Instance()->Registry->DeniedLookupCount();
            const auto imports    = engine.ImportCoordinator->DeniedImportCount();
            m_source_audit_passed = !vfs && !registry && !imports;
            ZENGINE_CORE_INFO("[CookedSourceAudit] vfs_denied={} registry_denied={} imports_denied={}", vfs, registry, imports)
            if (!m_source_audit_passed)
                ZENGINE_CORE_ERROR("[CookedRuntime] Source-independence audit observed denied access")
        }
    }

    void StandaloneRuntimeApplication::OnClosed()
    {
        if (m_cache_initialized)
        {
            while (m_cache.PumpOne())
            {
            }
            const bool closed = m_cache.Shutdown();
            ZENGINE_VALIDATE_ASSERT(closed, "Runtime cooked cache still has retained assets")
            (void) closed;
        }
        if (m_vfs_initialized)
            m_vfs.Shutdown();
        if (m_metadata.Pool)
            m_metadata.Shutdown();
        m_capture_pixels.reset();
    }
    bool StandaloneRuntimeApplication::UploadCancellationObserved() const noexcept
    {
        return m_upload_cancellation_observed;
    }

    bool StandaloneRuntimeApplication::CpuCancellationObserved() const noexcept
    {
        return m_cpu_cancellation_observed;
    }

    void StandaloneRuntimeApplication::EnableUploadSubmissionRejectionProbe() noexcept
    {
        m_reject_upload_probe = true;
    }

    void StandaloneRuntimeApplication::EnableStagingRefusalProbe() noexcept
    {
        m_fail_staging_probe = true;
    }

    void StandaloneRuntimeApplication::EnablePreflightRefusalProbe() noexcept
    {
        m_fail_preflight_probe = true;
    }

    void StandaloneRuntimeApplication::EnableDeviceLossProbe() noexcept
    {
        m_device_loss_probe = true;
    }

    void StandaloneRuntimeApplication::EnableSourceIndependenceAudit() noexcept
    {
        m_source_audit = true;
    }

    bool StandaloneRuntimeApplication::WantsSourceIndependenceAudit() const
    {
        return m_source_audit;
    }

    bool StandaloneRuntimeApplication::SourceAuditPassed() const noexcept
    {
        return m_source_audit_passed;
    }

    void StandaloneRuntimeApplication::ConfigureSteadyFrames(uint32_t frames) noexcept
    {
        m_steady_frames = frames;
    }

    void StandaloneRuntimeApplication::ConfigurePostSteadyFrames(uint32_t frames) noexcept
    {
        m_post_steady_frames = frames;
    }

    void StandaloneRuntimeApplication::EnableLifecycleStress() noexcept
    {
        m_lifecycle_stress = true;
    }

    void StandaloneRuntimeApplication::EnableFixedTestWindow() noexcept
    {
        m_fixed_test_window = true;
    }

    void StandaloneRuntimeApplication::EnableGpuCompletionObservation() noexcept
    {
        m_observe_gpu_completion = true;
    }

    void StandaloneRuntimeApplication::EnableShutdownProbe(bool gpu) noexcept
    {
        m_shutdown_probe_cpu = !gpu;
        m_shutdown_probe_gpu = gpu;
    }

    bool StandaloneRuntimeApplication::ShutdownProbePassed() const noexcept
    {
        return m_shutdown_probe_passed;
    }

    bool StandaloneRuntimeApplication::StagingRefusalObserved() const noexcept
    {
        return m_staging_refusal_observed;
    }

    bool StandaloneRuntimeApplication::FrameRecorded() const noexcept
    {
        return m_frame_recorded;
    }

    void StandaloneRuntimeApplication::ConfigureCapture(std::string path)
    {
        m_capture_path = std::move(path);
    }

    void StandaloneRuntimeApplication::OnEvent(Core::CoreEvent&) {}

    void StandaloneRuntimeApplication::OnPreRender() {}

    void StandaloneRuntimeApplication::OnPostRender() {}

    bool StandaloneRuntimeApplication::ShouldSimulate() const
    {
        return false;
    }

} // namespace ZEngine::Applications

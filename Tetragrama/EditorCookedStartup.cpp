#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorCookedStartup.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <ZEngine/Profiling/Profiling.h>
#include <chrono>
#include <cstdio>
#include <limits>
#include <thread>

namespace Tetragrama
{
    namespace
    {
        // Explicit development limits, not a qualified runtime profile.
        constexpr size_t CpuBytes        = ZMega(1024ULL);
        constexpr size_t GpuStagingBytes = ZMega(256ULL);
        constexpr size_t MetadataBytes   = ZMega(16ULL);
        constexpr size_t ObjectBytes     = ZMega(512ULL);
    } // namespace

    bool EditorCookedStartup::Initialize(Editor& editor, ZEngine::EngineContext& engine, const char* directory, const uuids::uuid& scene)
    {
        if (!directory || !directory[0] || scene.is_nil() || !engine.SceneSchemas || !engine.SceneDocumentWorkingBytes || engine.SceneDocumentWorkingBytes > std::numeric_limits<size_t>::max() - ZEngine::ECS::SceneStorageLease::MinimumBytes || !engine.SceneSchemas->FreezeForWorker(&m_diagnostics))
            return false;
        if (m_cpu_allocator.Initialize(CpuBytes).Failed() || m_gpu_allocator.Initialize(GpuStagingBytes).Failed() || !m_metadata.TryInit(&editor.EditorArena, MetadataBytes))
            return false;
        const ZEngine::Managers::CookedCpuAssetLimits limits = {.MaxAssets = 8192, .MaxObjectBytes = ObjectBytes, .MaxMeshWorkspaceBytes = ObjectBytes};
        if (!m_cache.Initialize(m_cpu_route, *engine.SceneSchemas, limits, 2, 32, 8192))
            return false;
        m_backend.Initialize(directory, ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::List, &editor.EditorArena);
        m_vfs.Initialize(&editor.EditorArena);
        m_vfs_initialized = true;
        if (m_vfs.Mount(&m_backend, ZEngine::Core::VFS::VFSPath::Root(), 0).Failed() || m_vfs.AcquireReadRoot(ZEngine::Core::VFS::VFSPath::Root(), &m_root).Failed())
            return false;
        if (engine.AdmitSceneStorage(&m_storage, engine.SceneDocumentWorkingBytes + ZEngine::ECS::SceneStorageLease::MinimumBytes) != ZEngine::ECS::SceneStorageAdmission::Acquired)
            return false;
        m_engine = &engine;
        m_scene  = scene;
        m_epoch  = editor.Session.SceneEpoch();
        return AdmitRequest();
    }

    EditorCookedStartup::RestartResult EditorCookedStartup::Restart(Editor& editor, const uuids::uuid& scene, ZEngine::ECS::SceneRequestId* request)
    {
        if (!m_engine || m_engine->CloseRequested.value.load(std::memory_order_acquire) || !editor.Session.Ready() || scene.is_nil())
            return RestartResult::Failed;
        const auto                   generation  = m_engine->ScenePublication.Generation();
        const auto                   admitted_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        ZEngine::ECS::SceneRequestId admitted    = {};
        if (!m_engine->SceneLoads.Admit(generation, &admitted))
            return RestartResult::Busy;
        // Admit atomically supersedes older requests; their payloads remain
        // owned until their worker or render work has actually drained.
        ReleaseWaiting();
        m_waiting_request            = admitted;
        m_waiting_scene              = scene;
        m_waiting_epoch              = editor.Session.SceneEpoch();
        m_waiting_generation         = generation;
        m_waiting_admission_ns       = admitted_ns;
        m_waiting_retain_result      = request != nullptr;
        m_waiting_pause_cpu          = false;
        m_waiting_supersession_probe = false;
        if (request)
            *request = admitted;
        return RestartResult::Started;
    }

    void EditorCookedStartup::ReleaseWaiting() noexcept
    {
        if (!m_waiting_request)
            return;
        auto& loads = m_engine->SceneLoads;
        if (loads.Pending(m_waiting_request))
            loads.Cancel(m_waiting_request);
        if (!m_waiting_retain_result)
            loads.Acknowledge(m_waiting_request);
        m_waiting_request            = {};
        m_waiting_retain_result      = false;
        m_waiting_pause_cpu          = false;
        m_waiting_supersession_probe = false;
    }

    void EditorCookedStartup::StartWaiting(Editor& editor)
    {
        if (!m_waiting_request)
            return;
        auto& loads = m_engine->SceneLoads;
        if (!loads.Pending(m_waiting_request) || m_waiting_generation != m_engine->ScenePublication.Generation() || m_waiting_epoch != editor.Session.SceneEpoch() || m_engine->CloseRequested.value.load(std::memory_order_acquire))
        {
            ReleaseWaiting();
            m_success = false;
            return;
        }
        if (!m_done || m_request || editor.SceneOpenWorkPending() || m_candidate || m_reconstruction.Instance || m_reconstruction.Assets || m_root.IsValid() || m_storage.Arena())
            return;
        const auto storage = m_engine->AdmitSceneStorage(&m_storage, m_engine->SceneDocumentWorkingBytes + ZEngine::ECS::SceneStorageLease::MinimumBytes);
        if (storage == ZEngine::ECS::SceneStorageAdmission::Busy)
            return;
        if (storage != ZEngine::ECS::SceneStorageAdmission::Acquired)
        {
            loads.Fail(m_waiting_request, ZEngine::ECS::SceneLoadStage::Admission, ZEngine::ECS::SceneLoadError::Resources);
            ReleaseWaiting();
            m_success = false;
            return;
        }
        if (m_vfs.AcquireReadRoot(ZEngine::Core::VFS::VFSPath::Root(), &m_root).Failed())
        {
            m_storage.Reset();
            loads.Fail(m_waiting_request, ZEngine::ECS::SceneLoadStage::Admission, ZEngine::ECS::SceneLoadError::Source);
            ReleaseWaiting();
            m_success = false;
            return;
        }
        if (!loads.TryStart(m_waiting_request))
        {
            m_root.Reset();
            m_storage.Reset();
            return;
        }
        m_scene                      = m_waiting_scene;
        m_epoch                      = m_waiting_epoch;
        m_generation                 = m_waiting_generation;
        m_admission_ns               = m_waiting_admission_ns;
        m_request                    = m_waiting_request;
        m_retain_result              = m_waiting_retain_result;
        m_pause_cpu                  = m_waiting_pause_cpu;
        m_cpu_supersession_probe     = m_waiting_supersession_probe;
        m_waiting_request            = {};
        m_waiting_retain_result      = false;
        m_waiting_pause_cpu          = false;
        m_waiting_supersession_probe = false;
        m_diagnostics                = {};
        m_cancelled.store(false, std::memory_order_release);
        m_failure_stage = ZEngine::ECS::SceneLoadStage::Read;
        m_ready.store(false, std::memory_order_relaxed);
        m_cache_cancel_token_ready.store(false, std::memory_order_relaxed);
        m_cpu_boundary.store(false, std::memory_order_relaxed);
        m_dispatched = false;
        m_success    = false;
        m_done       = false;
    }

    bool EditorCookedStartup::AdmitRequest() noexcept
    {
        m_generation   = m_engine->ScenePublication.Generation();
        m_admission_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (!m_engine->SceneLoads.Admit(m_generation, &m_request))
            return false;
        if (m_engine->SceneLoads.TryStart(m_request))
            return true;
        m_engine->SceneLoads.Fail(m_request, ZEngine::ECS::SceneLoadStage::Admission, ZEngine::ECS::SceneLoadError::Unavailable);
        m_engine->SceneLoads.Acknowledge(m_request);
        m_request = {};
        return false;
    }

    void EditorCookedStartup::ReleaseRequest() noexcept
    {
        if (!m_request)
            return;
        auto& loads = m_engine->SceneLoads;
        if (loads.Pending(m_request))
            loads.Cancel(m_request);
        const bool cleaned = loads.FinishCleanup(m_request);
        ZENGINE_VALIDATE_ASSERT(cleaned, "Cooked startup cleanup lost its admitted request")
        (void) cleaned;
        if (!m_retain_result)
            loads.Acknowledge(m_request);
        m_request       = {};
        m_retain_result = false;
    }

    bool EditorCookedStartup::SampleMemory(MemorySnapshot* output) const noexcept
    {
        if (!output || !m_done || !m_success || !m_ready.load(std::memory_order_acquire))
            return false;
        const auto cpu      = m_cpu_allocator.Statistics();
        const auto staging  = m_gpu_allocator.Statistics();
        const auto metadata = m_metadata.GetStatistics();
        *output             = {cpu.LiveBytes, cpu.PeakBytes, staging.LiveBytes, staging.PeakBytes, metadata.LiveBytes, metadata.LiveBlocks};
        return true;
    }

    void EditorCookedStartup::Run(void* context)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.CookedRequest");
        auto&      self         = *static_cast<EditorCookedStartup*>(context);
        const auto worker_begin = std::chrono::steady_clock::now();
        {
            ZEngine::Managers::CookedPackageSnapshot package     = {};
            ZEngine::Cook::CookDiagnostics           diagnostics = {};
            const ZEngine::Cook::CookLimits          limits      = {.MaxWorkingBytes = CpuBytes};
            const auto                               captured    = ZEngine::Managers::CookedPackageSnapshot::Capture(std::move(self.m_root), self.m_scene, *self.m_engine->SceneSchemas, limits, self.m_cpu_allocator, &diagnostics, &package, &self.m_cancelled);
            const auto                               capture_end = std::chrono::steady_clock::now();
            if (captured.Failed())
            {
                ZENGINE_CORE_ERROR("[Editor] Cooked package capture failed ({})", static_cast<unsigned>(captured.Error()))
                for (uint32_t i = 0; i < diagnostics.Count; ++i)
                    ZENGINE_CORE_ERROR("[Editor] {}", diagnostics.Entries[i].Message)
            }
            if (captured.Succeeded() && !self.m_cancelled.load(std::memory_order_acquire))
            {
                self.m_failure_stage = ZEngine::ECS::SceneLoadStage::Assets;
                // Reclaim closures retired by earlier scenes before bounded cache
                // admission. A cache hit can already be Ready and needs no pump.
                while (self.m_cache.PumpOne())
                {
                }
                char        path[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
                const auto* scene                                  = package.Scene();
                const int   length                                 = scene ? std::snprintf(path, sizeof(path), "/%s", scene->Source.CStr()) : -1;
                const auto  source                                 = length > 0 && static_cast<size_t>(length) < sizeof(path) ? ZEngine::Core::VFS::VFSPath::Parse(path) : ZEngine::Core::VFS::VFSResult<ZEngine::Core::VFS::VFSPath>::Fail(ZEngine::Core::VFS::VFSError::InvalidPath);
                const auto  reconstructed                          = source.Succeeded() ? ZEngine::ECS::ReconstructCookedScene(package, std::move(self.m_storage), *self.m_engine->SceneSchemas, self.m_cpu_route, ObjectBytes, self.m_engine->SceneDocumentWorkingBytes, self.m_cache, &self.m_reconstruction, &self.m_diagnostics, {}, &self.m_cancelled) : ZEngine::ECS::CookedSceneReconstructionError::InvalidInput;
                const auto  reconstruction_end                     = std::chrono::steady_clock::now();
                if (reconstructed != ZEngine::ECS::CookedSceneReconstructionError::None)
                {
                    ZENGINE_CORE_ERROR("[Editor] Cooked scene reconstruction failed ({})", static_cast<unsigned>(reconstructed))
                }
                else if (!self.m_cancelled.load(std::memory_order_acquire))
                {
                    self.m_cache_cancel_token = self.m_reconstruction.Assets.CancellationIdentity();
                    self.m_cache_cancel_token_ready.store(true, std::memory_order_release);
                    if (self.m_pause_cpu)
                    {
                        self.m_cpu_boundary.store(true, std::memory_order_release);
                        while (!self.m_cancelled.load(std::memory_order_acquire))
                            std::this_thread::yield();
                    }
                    for (;;)
                    {
                        if (self.m_cancelled.load(std::memory_order_acquire))
                            break;
                        const auto state = EditorSceneCandidate::PrepareReconstructedCooked(self.m_reconstruction, source.Value(), &self.m_candidate, &self.m_diagnostics);
                        if (state != CookedCandidateState::Pending)
                        {
                            self.m_success = state == CookedCandidateState::Ready;
                            break;
                        }
                        if (self.m_cancelled.load(std::memory_order_acquire) || !self.m_cache.PumpOne(&self.m_cancelled))
                            break;
                    }
                }
                for (uint32_t i = 0; i < self.m_diagnostics.Count; ++i)
                    ZENGINE_CORE_ERROR("[Editor] {}", self.m_diagnostics.Entries[i].Message)
                const auto preparation_end = std::chrono::steady_clock::now();
                const auto ns              = [](auto duration) { return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(); };
                ZENGINE_CORE_INFO("[CookedCpuTiming] capture_ns={} reconstruction_ns={} asset_preparation_ns={} worker_ns={}", ns(capture_end - worker_begin), ns(reconstruction_end - capture_end), ns(preparation_end - reconstruction_end), ns(preparation_end - worker_begin))
            }
        } // Destroy any cache-hit package before publishing worker completion.
        self.m_ready.store(true, std::memory_order_release);
    }

    void EditorCookedStartup::Tick(Editor& editor)
    {
        if (m_done)
            StartWaiting(editor);
        if (m_done)
            return;
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.CookedStartup");
        if (m_pause_cpu && !m_cpu_supersession_probe && !m_cpu_cancel_observed && m_cpu_boundary.load(std::memory_order_acquire))
        {
            ZEngine::ECS::SceneLoadResult result = {};
            if (m_engine->SceneLoads.Poll(m_request, &result) && result.CleanupPending && m_engine->SceneLoads.Cancel(m_request) == ZEngine::ECS::SceneCancelResult::Cancelled && m_engine->SceneLoads.Poll(m_request, &result) && result.CleanupPending)
            {
                m_cpu_cancel_observed = true;
                ZENGINE_CORE_INFO("[Editor] Cancelled cooked replacement during CPU preparation with worker cleanup pending")
            }
            m_cancelled.store(true, std::memory_order_release);
        }
        if (!m_engine->SceneLoads.Pending(m_request) || m_generation != m_engine->ScenePublication.Generation() || m_epoch != editor.Session.SceneEpoch())
            m_cancelled.store(true, std::memory_order_release);
        if (m_cancelled.load(std::memory_order_acquire))
        {
            if (m_cache_cancel_token_ready.load(std::memory_order_acquire))
                (void) m_cache.CancelUnsharedBuild(m_cache_cancel_token);
            if (m_dispatched && !m_ready.load(std::memory_order_acquire))
                return;
            m_candidate.reset();
            m_reconstruction = {};
            m_storage.Reset();
            m_root.Reset();
            ReleaseRequest();
            m_done    = true;
            m_success = false;
            return;
        }
        if (!m_dispatched)
        {
            auto* pool = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
            if (pool && pool->Submit(this, &Run))
                m_dispatched = true;
            return;
        }
        if (!m_ready.load(std::memory_order_acquire))
            return;
        m_done                                     = true;
        const bool                   reconstructed = m_success;
        ZEngine::ECS::SceneRequestId transferred   = {};
        if (!reconstructed || !editor.RequestCookedSceneOpen(m_candidate, std::move(m_reconstruction.Assets), m_epoch, m_gpu_route, m_metadata, ZEngine::Rendering::RenderResourceManager::SceneCookedTextureCapacity, m_retain_result ? &transferred : nullptr, m_request))
        {
            m_success = false;
            m_engine->SceneLoads.Fail(m_request, reconstructed ? ZEngine::ECS::SceneLoadStage::Preparation : m_failure_stage, reconstructed ? ZEngine::ECS::SceneLoadError::Resources : ZEngine::ECS::SceneLoadError::Source);
            m_candidate.reset();
            m_reconstruction = {};
            m_storage.Reset();
            m_root.Reset();
            ReleaseRequest();
            ZENGINE_CORE_ERROR("[Editor] Cooked scene startup failed; the current scene was preserved")
        }
        else
        {
            m_request       = {}; // Cleanup ownership moved to CookedOpenWork.
            m_retain_result = false;
        }
    }

    void EditorCookedStartup::Cancel() noexcept
    {
        m_cancelled.store(true, std::memory_order_release);
        if (m_cache_cancel_token_ready.load(std::memory_order_acquire))
            (void) m_cache.CancelUnsharedBuild(m_cache_cancel_token);
    }

    void EditorCookedStartup::AfterWorkersJoined() noexcept
    {
        m_candidate.reset();
        m_reconstruction = {};
        m_storage        = {};
        m_root.Reset();
        ReleaseRequest();
        ReleaseWaiting();
    }

    void EditorCookedStartup::AfterDeviceShutdown() noexcept
    {
        while (m_cache.PumpOne())
        {
        }
        const bool closed = m_cache.Shutdown();
        ZENGINE_VALIDATE_ASSERT(closed, "Cooked startup cache still has retained scene resources")
        (void) closed;
        if (m_vfs_initialized)
            m_vfs.Shutdown();
        if (m_metadata.Pool)
            m_metadata.Shutdown();
    }
} // namespace Tetragrama

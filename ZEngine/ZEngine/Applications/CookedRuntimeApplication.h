#pragma once
#include <ZEngine/Applications/CookedRuntimeReconstruction.h>
#include <ZEngine/Applications/CookedRuntimeSceneHost.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/ECS/SceneLoadCoordinator.h>
#include <ZEngine/Rendering/Scenes/CookedSceneResourceRequest.h>
#include <string>

namespace ZEngine::Applications
{
    /// Renderer-only cooked process. Gameplay simulation remains disabled.
    class CookedRuntimeApplication final : public GameApplication
    {
    public:
        void                                 Configure(std::string directory, uuids::uuid scene, uuids::uuid camera, bool exit_after_frame, uint32_t expected_draws, uint32_t reloads = 0, uuids::uuid alternate = {}, bool retry_failure_probe = false, bool cancel_upload_probe = false, bool cancel_cpu_probe = false, bool supersede_cpu_probe = false, bool supersede_gpu_probe = false, bool supersede_waiting_probe = false);
        /// Main-thread admission. A newer request supersedes older work, which
        /// drains before the latest waiting request starts.
        /// When output is supplied, the terminal result stays in the bounded
        /// ledger until explicitly acknowledged. Refusal preserves output.
        [[nodiscard]] bool                   RequestScene(const uuids::uuid& scene, ECS::SceneRequestId* output = nullptr);
        /// Cancellation publishes a terminal outcome immediately; the request
        /// remains busy until its worker and GPU payload have drained.
        [[nodiscard]] ECS::SceneCancelResult CancelSceneRequest() noexcept;
        [[nodiscard]] ECS::SceneCancelResult CancelSceneRequest(ECS::SceneRequestId request) noexcept;
        [[nodiscard]] bool                   PollSceneRequest(ECS::SceneRequestId request, ECS::SceneLoadResult* output) const noexcept;
        [[nodiscard]] bool                   AcknowledgeSceneRequest(ECS::SceneRequestId request) noexcept;
        [[nodiscard]] bool                   UploadCancellationObserved() const noexcept
        {
            return m_upload_cancellation_observed;
        }
        [[nodiscard]] bool CpuCancellationObserved() const noexcept
        {
            return m_cpu_cancellation_observed;
        }
        void EnableUploadSubmissionRejectionProbe() noexcept
        {
            m_reject_upload_probe = true;
        }
        void EnableStagingRefusalProbe() noexcept
        {
            m_fail_staging_probe = true;
        }
        void EnablePreflightRefusalProbe() noexcept
        {
            m_fail_preflight_probe = true;
        }
        void EnableDeviceLossProbe() noexcept
        {
            m_device_loss_probe = true;
        }
        void EnableSourceIndependenceAudit() noexcept
        {
            m_source_audit = true;
        }
        [[nodiscard]] bool WantsSourceIndependenceAudit() const override
        {
            return m_source_audit;
        }
        [[nodiscard]] bool SourceAuditPassed() const noexcept
        {
            return m_source_audit_passed;
        }
        void ConfigureSteadyFrames(uint32_t frames) noexcept
        {
            m_steady_frames = frames;
        }
        void ConfigurePostSteadyFrames(uint32_t frames) noexcept
        {
            m_post_steady_frames = frames;
        }
        void EnableLifecycleStress() noexcept
        {
            m_lifecycle_stress = true;
        }
        void EnableFixedTestWindow() noexcept
        {
            m_fixed_test_window = true;
        }
        void EnableGpuCompletionObservation() noexcept
        {
            m_observe_gpu_completion = true;
        }
        void EnableShutdownProbe(bool gpu) noexcept
        {
            m_shutdown_probe_cpu = !gpu;
            m_shutdown_probe_gpu = gpu;
        }
        [[nodiscard]] bool ShutdownProbePassed() const noexcept
        {
            return m_shutdown_probe_passed;
        }
        [[nodiscard]] bool StagingRefusalObserved() const noexcept
        {
            return m_staging_refusal_observed;
        }
        [[nodiscard]] bool FrameRecorded() const noexcept
        {
            return m_frame_recorded;
        }
        void ConfigureCapture(std::string path)
        {
            m_capture_path = std::move(path);
        }
        void               OverrideWindowConfiguration() override;
        void               OnInitializing() override;
        void               OnInitialized() override;
        void               OnEvent(Core::CoreEvent&) override {}
        void               OnUpdate(float) override;
        void               OnPreRender() override {}
        void               OnPostRender() override {}
        void               OnRenderUI() override;
        void               PrepareScene(RenderFrameState&) override;
        void               OnClosing() override;
        void               OnClosed() override;
        void               OnWorldShutdown(EngineContext&) override;
        [[nodiscard]] bool ShouldSimulate() const override
        {
            return false;
        }

    private:
        static void                                            RunWorker(void*);
        static void                                            DrainCacheWorker(void*);
        static bool                                            BeginCapture(void*, uint32_t, uint32_t, uint64_t);
        static void                                            CompleteCapture(const void*, size_t, void*);
        static void                                            CancelCapture(void*);
        void                                                   StartWaiting();
        void                                                   RestoreStagingLimit(bool request_failed) noexcept;
        void                                                   Fail(const char* message, ECS::SceneLoadStage stage = ECS::SceneLoadStage::Preparation);
        Core::Memory::ArenaAllocator                           m_arena         = {};
        Cook::CookPageAllocator                                m_cpu_allocator = {};
        Cook::CookPageAllocator                                m_gpu_allocator = {};
        Cook::CookCodecAllocationRoute                         m_cpu_route{m_cpu_allocator};
        Cook::CookCodecAllocationRoute                         m_gpu_route{m_gpu_allocator};
        Core::Memory::TLSFSlab                                 m_metadata                     = {};
        Core::VFS::VFSDiskBackend                              m_backend                      = {};
        Core::VFS::VFSContext                                  m_vfs                          = {};
        Core::VFS::VFSReadRootLease                            m_root                         = {};
        Managers::CookedCpuClosureCache                        m_cache                        = {};
        ECS::SceneStorageLease                                 m_storage                      = {};
        Rendering::Scenes::RenderScene                         m_bootstrap                    = {};
        Rendering::Cameras::CameraFrameData                    m_bootstrap_camera             = {};
        std::unique_ptr<Rendering::Scenes::CookedRuntimeScene> m_candidate                    = {};
        Managers::CookedCpuClosureCache::Lease                 m_assets                       = {};
        Rendering::Scenes::CookedSceneResourceRequest          m_gpu                          = {};
        CookedRuntimeSceneHost                                 m_host                         = {};
        Cook::CookDiagnostics                                  m_cook_diagnostics             = {};
        ECS::SceneDiagnostics                                  m_scene_diagnostics            = {};
        CookedRuntimeReconstructionError                       m_worker_result                = CookedRuntimeReconstructionError::InvalidInput;
        std::atomic<bool>                                      m_cancelled                    = false;
        std::atomic<bool>                                      m_worker_done                  = false;
        std::atomic<bool>                                      m_cache_drain_running          = false;
        std::atomic<bool>                                      m_cpu_boundary                 = false;
        std::string                                            m_directory                    = {};
        std::string                                            m_capture_path                 = {};
        std::unique_ptr<uint8_t[]>                             m_capture_pixels               = {};
        std::atomic<uint32_t>                                  m_capture_state                = 0;
        size_t                                                 m_capture_bytes                = 0;
        uint32_t                                               m_capture_width                = 0;
        uint32_t                                               m_capture_height               = 0;
        uint64_t                                               m_capture_generation           = 0;
        uuids::uuid                                            m_scene                        = {};
        uuids::uuid                                            m_initial_scene                = {};
        uuids::uuid                                            m_active_scene                 = {};
        uuids::uuid                                            m_alternate_scene              = {};
        uuids::uuid                                            m_camera                       = {};
        ECS::SceneRequestId                                    m_request                      = {};
        ECS::SceneRequestId                                    m_waiting_request              = {};
        bool                                                   m_request_retain_result        = false;
        bool                                                   m_waiting_retain_result        = false;
        ECS::SceneLoadOutcome                                  m_request_terminal             = ECS::SceneLoadOutcome::Pending;
        ECS::SceneRequestId                                    m_probe_old                    = {};
        ECS::SceneRequestId                                    m_probe_latest                 = {};
        uuids::uuid                                            m_waiting_scene                = {};
        uint64_t                                               m_waiting_generation           = 0;
        uint64_t                                               m_waiting_admission_ns         = 0;
        uint64_t                                               m_generation                   = 0;
        uint64_t                                               m_admission_ns                 = 0;
        uint64_t                                               m_active_admission_ns          = 0;
        uint64_t                                               m_completed_generation         = 0;
        uint64_t                                               m_completion_observed_ns       = 0;
        bool                                                   m_observe_gpu_completion       = false;
        uint64_t                                               m_reported_generation          = 0;
        uint64_t                                               m_steady_start_ns              = 0;
        uint64_t                                               m_steady_start_count           = 0;
        uint64_t                                               m_steady_generation            = 0;
        uint32_t                                               m_steady_frames                = 0;
        bool                                                   m_steady_complete              = false;
        uint64_t                                               m_post_steady_start_ns         = 0;
        uint64_t                                               m_post_steady_start_count      = 0;
        uint64_t                                               m_post_steady_generation       = 0;
        uint32_t                                               m_post_steady_frames           = 0;
        bool                                                   m_post_steady_complete         = false;
        uint64_t                                               m_resize_sequence              = 1;
        uint32_t                                               m_width                        = 0;
        uint32_t                                               m_height                       = 0;
        uint32_t                                               m_expected_draws               = 0;
        uint32_t                                               m_reloads_remaining            = 0;
        uint32_t                                               m_stress_admitted              = 0;
        uint32_t                                               m_stress_cancelled             = 0;
        uint32_t                                               m_stress_committed             = 0;
        uint64_t                                               m_stress_cancel_generation     = 0;
        uint64_t                                               m_stress_cancel_requested_ns   = 0;
        uint64_t                                               m_stress_submission_baseline   = 0;
        bool                                                   m_lifecycle_stress             = false;
        bool                                                   m_fixed_test_window            = false;
        bool                                                   m_shutdown_probe_cpu           = false;
        bool                                                   m_shutdown_probe_gpu           = false;
        bool                                                   m_shutdown_probe_started       = false;
        bool                                                   m_shutdown_probe_passed        = false;
        uint64_t                                               m_shutdown_probe_generation    = 0;
        bool                                                   m_stress_waiting_frame         = false;
        uint8_t                                                m_last_gpu_status              = 255;
        bool                                                   m_publication_wait_logged      = false;
        bool                                                   m_exit_after_frame             = false;
        bool                                                   m_frame_recorded               = false;
        bool                                                   m_initialized                  = false;
        bool                                                   m_vfs_initialized              = false;
        bool                                                   m_cache_initialized            = false;
        bool                                                   m_dispatched                   = false;
        bool                                                   m_gpu_started                  = false;
        bool                                                   m_failed                       = false;
        bool                                                   m_retry_failure_probe          = false;
        bool                                                   m_cancel_upload_probe          = false;
        bool                                                   m_upload_cancellation_observed = false;
        bool                                                   m_cancel_cpu_probe             = false;
        bool                                                   m_cpu_pause_armed              = false;
        bool                                                   m_cpu_cancellation_observed    = false;
        bool                                                   m_supersede_cpu_probe          = false;
        bool                                                   m_supersede_gpu_probe          = false;
        bool                                                   m_supersede_waiting_probe      = false;
        bool                                                   m_reject_upload_probe          = false;
        bool                                                   m_device_loss_probe            = false;
        bool                                                   m_source_audit                 = false;
        bool                                                   m_source_audit_passed          = false;
        bool                                                   m_fail_staging_probe           = false;
        bool                                                   m_fail_preflight_probe         = false;
        bool                                                   m_staging_limit_active         = false;
        bool                                                   m_staging_gpu_started          = false;
        bool                                                   m_staging_refusal_observed     = false;
        uint64_t                                               m_staging_refusal_baseline     = 0;
        Core::Memory::GpuAllocationLimits                      m_saved_gpu_limits             = {};
        bool                                                   m_waiting_superseded_observed  = false;
        bool                                                   m_cpu_supersede_armed          = false;
        bool                                                   m_waiting_failure_frame        = false;
        uint64_t                                               m_failure_submission_baseline  = 0;
    };
} // namespace ZEngine::Applications

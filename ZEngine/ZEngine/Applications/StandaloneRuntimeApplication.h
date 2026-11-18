#pragma once
#include <ZEngine/Applications/CookedRuntimeReconstruction.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Applications/RuntimeSceneHost.h>
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
    /// @brief Standalone, renderer-only runtime without an editor session.
    /// @details The current loader consumes cooked packages. Gameplay simulation
    /// remains disabled until the runtime gameplay services are integrated.
    class StandaloneRuntimeApplication final : public GameApplication
    {
    public:
        /// @brief Configure the initial cooked scene and optional lifecycle probes.
        /// @details Configure before initialization; test probes are opt-in.
        void                                 Configure(std::string directory, uuids::uuid scene, uuids::uuid camera, bool exit_after_frame, uint32_t expected_draws, uint32_t reloads = 0, uuids::uuid alternate = {}, bool retry_failure_probe = false, bool cancel_upload_probe = false, bool cancel_cpu_probe = false, bool supersede_cpu_probe = false, bool supersede_gpu_probe = false, bool supersede_waiting_probe = false);
        /// @brief Admit a scene request on the main thread.
        /// @details A newer request supersedes older work, which drains before
        /// the waiting request starts. An output ID retains its terminal result
        /// until acknowledged; refusal preserves the output.
        [[nodiscard]] bool                   RequestScene(const uuids::uuid& scene, ECS::SceneRequestId* output = nullptr);
        /// @brief Cancel the most recent pending scene request.
        /// @details The outcome is published immediately; cleanup remains pending
        /// until the worker and GPU payload have drained.
        [[nodiscard]] ECS::SceneCancelResult CancelSceneRequest() noexcept;
        /// @brief Cancel the specified pending scene request.
        [[nodiscard]] ECS::SceneCancelResult CancelSceneRequest(ECS::SceneRequestId request) noexcept;
        /// @brief Read the retained outcome for a scene request.
        [[nodiscard]] bool                   PollSceneRequest(ECS::SceneRequestId request, ECS::SceneLoadResult* output) const noexcept;
        /// @brief Release a retained terminal request outcome.
        [[nodiscard]] bool                   AcknowledgeSceneRequest(ECS::SceneRequestId request) noexcept;
        /// @brief Report whether the upload cancellation probe reached its boundary.
        [[nodiscard]] bool                   UploadCancellationObserved() const noexcept;
        /// @brief Report whether the CPU cancellation probe reached its boundary.
        [[nodiscard]] bool                   CpuCancellationObserved() const noexcept;
        /// @brief Inject a rejection before a recorded upload is submitted.
        void                                 EnableUploadSubmissionRejectionProbe() noexcept;
        /// @brief Inject a replacement staging-budget refusal.
        void                                 EnableStagingRefusalProbe() noexcept;
        /// @brief Inject a refusal before publication of a prepared replacement.
        void                                 EnablePreflightRefusalProbe() noexcept;
        /// @brief Inject device loss during the upload probe.
        void                                 EnableDeviceLossProbe() noexcept;
        /// @brief Enable the standalone source-access audit.
        void                                 EnableSourceIndependenceAudit() noexcept;
        /// @brief Report whether engine services should enforce the source audit.
        [[nodiscard]] bool                   WantsSourceIndependenceAudit() const override;
        /// @brief Report the final source-access audit result after shutdown.
        [[nodiscard]] bool                   SourceAuditPassed() const noexcept;
        /// @brief Configure steady frames to observe before replacement.
        void                                 ConfigureSteadyFrames(uint32_t frames) noexcept;
        /// @brief Configure steady frames to observe after replacement.
        void                                 ConfigurePostSteadyFrames(uint32_t frames) noexcept;
        /// @brief Enable the alternating cancellation and replacement probe.
        void                                 EnableLifecycleStress() noexcept;
        /// @brief Use the fixed-size window required by test captures.
        void                                 EnableFixedTestWindow() noexcept;
        /// @brief Observe GPU completion of submitted scene frames.
        void                                 EnableGpuCompletionObservation() noexcept;
        /// @brief Request shutdown while CPU or GPU preparation is pending.
        void                                 EnableShutdownProbe(bool gpu) noexcept;
        /// @brief Report whether the shutdown probe drained its pending request.
        [[nodiscard]] bool                   ShutdownProbePassed() const noexcept;
        /// @brief Report whether the staging refusal probe observed its failure.
        [[nodiscard]] bool                   StagingRefusalObserved() const noexcept;
        /// @brief Report whether runtime scene startup or replacement failed.
        [[nodiscard]] bool                   Failed() const noexcept;
        /// @brief Report whether the requested scene frame was recorded.
        [[nodiscard]] bool                   FrameRecorded() const noexcept;
        /// @brief Set the output path for the optional scene-target capture.
        void                                 ConfigureCapture(std::string path);
        /// @brief Apply the optional fixed test-window configuration.
        void                                 OverrideWindowConfiguration() override;
        /// @brief Configure application state before engine initialization.
        void                                 OnInitializing() override;
        /// @brief Initialize runtime services and admit the first scene.
        void                                 OnInitialized() override;
        /// @brief Ignore editor events in the standalone renderer.
        void                                 OnEvent(Core::CoreEvent&) override;
        /// @brief Advance scene admission, reconstruction and publication.
        void                                 OnUpdate(float) override;
        /// @brief No standalone pre-render hook is required.
        void                                 OnPreRender() override;
        /// @brief No standalone post-render hook is required.
        void                                 OnPostRender() override;
        /// @brief Display the runtime scene output in a full-window view.
        void                                 OnRenderUI() override;
        /// @brief Populate the render frame from the active runtime scene.
        void                                 PrepareScene(RenderFrameState&) override;
        /// @brief Begin cancellation and cleanup when the application closes.
        void                                 OnClosing() override;
        /// @brief Release application-owned allocations after engine shutdown.
        void                                 OnClosed() override;
        /// @brief Retire scene owners after engine readers have joined.
        void                                 OnWorldShutdown(EngineContext&) override;
        /// @brief Keep simulation disabled for the renderer-only runtime.
        [[nodiscard]] bool                   ShouldSimulate() const override;

    private:
        /// @brief Reconstruct the admitted cooked scene on a worker.
        static void                                      RunWorker(void*);
        /// @brief Drain queued cooked cache work on a worker.
        static void                                      DrainCacheWorker(void*);
        /// @brief Begin an optional render-target capture.
        static bool                                      BeginCapture(void*, uint32_t, uint32_t, uint64_t);
        /// @brief Complete a render-target capture with its readback bytes.
        static void                                      CompleteCapture(const void*, size_t, void*);
        /// @brief Cancel an outstanding render-target capture.
        static void                                      CancelCapture(void*);
        /// @brief Start the newest waiting request once its resources are available.
        void                                             StartWaiting();
        /// @brief Restore the staging budget after a refusal probe.
        void                                             RestoreStagingLimit(bool request_failed) noexcept;
        /// @brief Fail the current request and preserve the active scene when possible.
        void                                             Fail(const char* message, ECS::SceneLoadStage stage = ECS::SceneLoadStage::Preparation);
        Core::Memory::ArenaAllocator                     m_arena         = {};
        Cook::CookPageAllocator                          m_cpu_allocator = {};
        Cook::CookPageAllocator                          m_gpu_allocator = {};
        Cook::CookCodecAllocationRoute                   m_cpu_route{m_cpu_allocator};
        Cook::CookCodecAllocationRoute                   m_gpu_route{m_gpu_allocator};
        Core::Memory::TLSFSlab                           m_metadata                     = {};
        Core::VFS::VFSDiskBackend                        m_backend                      = {};
        Core::VFS::VFSContext                            m_vfs                          = {};
        Core::VFS::VFSReadRootLease                      m_root                         = {};
        Managers::CookedCpuClosureCache                  m_cache                        = {};
        ECS::SceneStorageLease                           m_storage                      = {};
        Rendering::Scenes::RenderScene                   m_bootstrap                    = {};
        Rendering::Cameras::CameraFrameData              m_bootstrap_camera             = {};
        std::unique_ptr<Rendering::Scenes::RuntimeScene> m_candidate                    = {};
        Managers::CookedCpuClosureCache::Lease           m_assets                       = {};
        Rendering::Scenes::CookedSceneResourceRequest    m_gpu                          = {};
        RuntimeSceneHost                                 m_host                         = {};
        Cook::CookDiagnostics                            m_cook_diagnostics             = {};
        ECS::SceneDiagnostics                            m_scene_diagnostics            = {};
        CookedRuntimeReconstructionError                 m_worker_result                = CookedRuntimeReconstructionError::InvalidInput;
        /// @brief Isolate worker handoff flags from independently written state.
        PaddedAtomic<bool>                               m_cancelled                    = {};
        PaddedAtomic<bool>                               m_worker_done                  = {};
        PaddedAtomic<bool>                               m_cache_drain_running          = {};
        std::atomic<bool>                                m_cpu_boundary                 = false;
        std::string                                      m_directory                    = {};
        std::string                                      m_capture_path                 = {};
        std::unique_ptr<uint8_t[]>                       m_capture_pixels               = {};
        std::atomic<uint32_t>                            m_capture_state                = 0;
        size_t                                           m_capture_bytes                = 0;
        uint32_t                                         m_capture_width                = 0;
        uint32_t                                         m_capture_height               = 0;
        uint64_t                                         m_capture_generation           = 0;
        uuids::uuid                                      m_scene                        = {};
        uuids::uuid                                      m_initial_scene                = {};
        uuids::uuid                                      m_active_scene                 = {};
        uuids::uuid                                      m_alternate_scene              = {};
        uuids::uuid                                      m_camera                       = {};
        ECS::SceneRequestId                              m_request                      = {};
        ECS::SceneRequestId                              m_waiting_request              = {};
        bool                                             m_request_retain_result        = false;
        bool                                             m_waiting_retain_result        = false;
        ECS::SceneLoadOutcome                            m_request_terminal             = ECS::SceneLoadOutcome::Pending;
        ECS::SceneRequestId                              m_probe_old                    = {};
        ECS::SceneRequestId                              m_probe_latest                 = {};
        uuids::uuid                                      m_waiting_scene                = {};
        uint64_t                                         m_waiting_generation           = 0;
        uint64_t                                         m_waiting_admission_ns         = 0;
        uint64_t                                         m_generation                   = 0;
        uint64_t                                         m_admission_ns                 = 0;
        uint64_t                                         m_active_admission_ns          = 0;
        uint64_t                                         m_completed_generation         = 0;
        uint64_t                                         m_completion_observed_ns       = 0;
        bool                                             m_observe_gpu_completion       = false;
        uint64_t                                         m_reported_generation          = 0;
        uint64_t                                         m_steady_start_ns              = 0;
        uint64_t                                         m_steady_start_count           = 0;
        uint64_t                                         m_steady_generation            = 0;
        uint32_t                                         m_steady_frames                = 0;
        bool                                             m_steady_complete              = false;
        uint64_t                                         m_post_steady_start_ns         = 0;
        uint64_t                                         m_post_steady_start_count      = 0;
        uint64_t                                         m_post_steady_generation       = 0;
        uint32_t                                         m_post_steady_frames           = 0;
        bool                                             m_post_steady_complete         = false;
        uint64_t                                         m_resize_sequence              = 1;
        uint32_t                                         m_width                        = 0;
        uint32_t                                         m_height                       = 0;
        uint32_t                                         m_expected_draws               = 0;
        uint32_t                                         m_reloads_remaining            = 0;
        uint32_t                                         m_stress_admitted              = 0;
        uint32_t                                         m_stress_cancelled             = 0;
        uint32_t                                         m_stress_committed             = 0;
        uint64_t                                         m_stress_cancel_generation     = 0;
        uint64_t                                         m_stress_cancel_requested_ns   = 0;
        uint64_t                                         m_stress_submission_baseline   = 0;
        bool                                             m_lifecycle_stress             = false;
        bool                                             m_fixed_test_window            = false;
        bool                                             m_shutdown_probe_cpu           = false;
        bool                                             m_shutdown_probe_gpu           = false;
        bool                                             m_shutdown_probe_started       = false;
        bool                                             m_shutdown_probe_passed        = false;
        uint64_t                                         m_shutdown_probe_generation    = 0;
        bool                                             m_stress_waiting_frame         = false;
        uint8_t                                          m_last_gpu_status              = 255;
        bool                                             m_publication_wait_logged      = false;
        bool                                             m_exit_after_frame             = false;
        bool                                             m_frame_recorded               = false;
        bool                                             m_initialized                  = false;
        bool                                             m_vfs_initialized              = false;
        bool                                             m_cache_initialized            = false;
        bool                                             m_dispatched                   = false;
        bool                                             m_gpu_started                  = false;
        bool                                             m_failed                       = false;
        bool                                             m_retry_failure_probe          = false;
        bool                                             m_cancel_upload_probe          = false;
        bool                                             m_upload_cancellation_observed = false;
        bool                                             m_cancel_cpu_probe             = false;
        bool                                             m_cpu_pause_armed              = false;
        bool                                             m_cpu_cancellation_observed    = false;
        bool                                             m_supersede_cpu_probe          = false;
        bool                                             m_supersede_gpu_probe          = false;
        bool                                             m_supersede_waiting_probe      = false;
        bool                                             m_reject_upload_probe          = false;
        bool                                             m_device_loss_probe            = false;
        bool                                             m_source_audit                 = false;
        bool                                             m_source_audit_passed          = false;
        bool                                             m_fail_staging_probe           = false;
        bool                                             m_fail_preflight_probe         = false;
        bool                                             m_staging_limit_active         = false;
        bool                                             m_staging_gpu_started          = false;
        bool                                             m_staging_refusal_observed     = false;
        uint64_t                                         m_staging_refusal_baseline     = 0;
        Core::Memory::GpuAllocationLimits                m_saved_gpu_limits             = {};
        bool                                             m_waiting_superseded_observed  = false;
        bool                                             m_cpu_supersede_armed          = false;
        bool                                             m_waiting_failure_frame        = false;
        uint64_t                                         m_failure_submission_baseline  = 0;
    };
} // namespace ZEngine::Applications

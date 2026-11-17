#pragma once
#include <Tetragrama/EditorSceneCandidate.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/ECS/SceneLoadCoordinator.h>
#include <atomic>

namespace Tetragrama
{
    struct Editor;

    // Process-lifetime startup owner. The package root is separate from the
    // project VFS and its cache and routes remain alive until device teardown.
    class EditorCookedStartup final
    {
    public:
        enum class RestartResult : uint8_t
        {
            Started,
            Busy,
            Failed
        };
        struct MemorySnapshot
        {
            uint64_t CpuLive        = 0;
            uint64_t CpuPeak        = 0;
            uint64_t StagingLive    = 0;
            uint64_t StagingPeak    = 0;
            size_t   MetadataLive   = 0;
            size_t   MetadataBlocks = 0;
        };
        bool                        Initialize(Editor&, ZEngine::EngineContext&, const char* package_directory, const uuids::uuid& scene);
        /// Called by the main owner after the previous request retired. The
        /// scene/cache allocators remain shared with still-live scene pins.
        [[nodiscard]] RestartResult Restart(Editor&, const uuids::uuid& scene, ZEngine::ECS::SceneRequestId* request = nullptr);
        /// @brief Report whether startup or a replacement request remains pending.
        [[nodiscard]] bool          Pending() const noexcept;
        /// @brief Report whether this owner belongs to the given engine.
        [[nodiscard]] bool          OwnsEngine(const ZEngine::EngineContext* engine) const noexcept;
        /// Owner-thread sample only after the worker has completed and its
        /// candidate has been transferred; it never races the CPU allocator.
        [[nodiscard]] bool          SampleMemory(MemorySnapshot* output) const noexcept;
        /// @brief Return the request admission timestamp.
        [[nodiscard]] uint64_t      AdmissionTimestampNs() const noexcept;
        /// @brief Return the generation assigned to the next scene.
        [[nodiscard]] uint64_t      ExpectedGeneration() const noexcept;
        /// @brief Pause the current or waiting request at the CPU cancellation boundary.
        void                        ArmCpuCancellationProbe() noexcept;
        /// @brief Pause the waiting request for the CPU supersession probe.
        void                        ArmCpuSupersessionProbe() noexcept;
        /// @brief Report whether the CPU probe reached its boundary.
        [[nodiscard]] bool          CpuProbeReached() const noexcept;
        /// @brief Report whether CPU cancellation was observed.
        [[nodiscard]] bool          CpuCancellationObserved() const noexcept;
        void                        Tick(Editor&);
        void                        Cancel() noexcept;
        void                        AfterWorkersJoined() noexcept;
        void                        AfterDeviceShutdown() noexcept;
        /// @brief Report whether startup ended without a successful scene.
        [[nodiscard]] bool          Failed() const noexcept;

    private:
        static void                                                 Run(void*);
        bool                                                        AdmitRequest() noexcept;
        void                                                        ReleaseRequest() noexcept;
        void                                                        ReleaseWaiting() noexcept;
        void                                                        StartWaiting(Editor&);
        ZEngine::Cook::CookPageAllocator                            m_cpu_allocator = {};
        ZEngine::Cook::CookPageAllocator                            m_gpu_allocator = {};
        ZEngine::Cook::CookCodecAllocationRoute                     m_cpu_route{m_cpu_allocator};
        ZEngine::Cook::CookCodecAllocationRoute                     m_gpu_route{m_gpu_allocator};
        ZEngine::Core::Memory::TLSFSlab                             m_metadata                   = {};
        ZEngine::Core::VFS::VFSDiskBackend                          m_backend                    = {};
        ZEngine::Core::VFS::VFSContext                              m_vfs                        = {};
        ZEngine::Core::VFS::VFSReadRootLease                        m_root                       = {};
        ZEngine::Managers::CookedCpuClosureCache                    m_cache                      = {};
        ZEngine::ECS::SceneStorageLease                             m_storage                    = {};
        ZEngine::ECS::CookedSceneReconstruction                     m_reconstruction             = {};
        ZEngine::Helpers::Scope<EditorSceneCandidate>               m_candidate                  = {};
        ZEngine::ECS::SceneDiagnostics                              m_diagnostics                = {};
        ZEngine::EngineContext*                                     m_engine                     = nullptr;
        uuids::uuid                                                 m_scene                      = {};
        uint64_t                                                    m_epoch                      = 0;
        uint64_t                                                    m_generation                 = 0;
        uint64_t                                                    m_admission_ns               = 0;
        ZEngine::ECS::SceneRequestId                                m_request                    = {};
        ZEngine::ECS::SceneRequestId                                m_waiting_request            = {};
        uuids::uuid                                                 m_waiting_scene              = {};
        uint64_t                                                    m_waiting_epoch              = 0;
        uint64_t                                                    m_waiting_generation         = 0;
        uint64_t                                                    m_waiting_admission_ns       = 0;
        ZEngine::Managers::CookedCpuClosureCache::CancellationToken m_cache_cancel_token         = {};
        ZEngine::ECS::SceneLoadStage                                m_failure_stage              = ZEngine::ECS::SceneLoadStage::Read;
        /// @brief Isolate worker handoff flags from independently written state.
        PaddedAtomic<bool>                                          m_cancelled                  = {};
        PaddedAtomic<bool>                                          m_ready                      = {};
        std::atomic<bool>                                           m_cache_cancel_token_ready   = false;
        std::atomic<bool>                                           m_cpu_boundary               = false;
        bool                                                        m_pause_cpu                  = false;
        bool                                                        m_waiting_pause_cpu          = false;
        bool                                                        m_cpu_supersession_probe     = false;
        bool                                                        m_waiting_supersession_probe = false;
        bool                                                        m_cpu_cancel_observed        = false;
        bool                                                        m_dispatched                 = false;
        bool                                                        m_retain_result              = false;
        bool                                                        m_waiting_retain_result      = false;
        bool                                                        m_done                       = false;
        bool                                                        m_success                    = false;
        bool                                                        m_vfs_initialized            = false;
    };
} // namespace Tetragrama

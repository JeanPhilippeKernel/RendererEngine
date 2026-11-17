#pragma once
#include <ZEngine/Rendering/CookedSceneGpuPreparation.h>
#include <atomic>

namespace ZEngine::Rendering
{
    /// Single producer / single render consumer. Services and each admitted
    /// route/slab/cache outlive the ticket until it is terminal. Seal and join
    /// producers before draining on the render owner or after it has joined.
    class CookedSceneGpuMailbox final
    {
    public:
        static constexpr uint32_t Capacity = 2;
        struct Ticket
        {
            uint32_t Slot   = UINT32_MAX;
            uint64_t Serial = 0;
            explicit operator bool() const noexcept
            {
                return Slot < Capacity && Serial;
            }
        };
        enum class Outcome : uint8_t
        {
            Pending,
            Submitted,
            Failed,
            Cancelled
        };
        enum class Admission : uint8_t
        {
            Queued,
            Backpressure,
            Failed
        };
        CookedSceneGpuMailbox()                                         = default;
        CookedSceneGpuMailbox(const CookedSceneGpuMailbox&)             = delete;
        CookedSceneGpuMailbox&  operator=(const CookedSceneGpuMailbox&) = delete;

        /// Producer-only. Failure leaves the ticket, lease and scene untouched.
        [[nodiscard]] bool      Begin(const Managers::CookedCpuClosureCache::Lease&, std::span<const uuids::uuid>, const Cook::CookAssetIdentity&, Core::Memory::IAllocationRoute&, Core::Memory::TLSFSlab&, RenderResourceManager&, Hardwares::VulkanDevice&, RenderResourceManager::SceneResourcePreparationId, uint32_t max_textures, Ticket* output);
        /// Distinguishes full mailbox capacity from invalid input or bounded
        /// allocation/initialization failure. Failure preserves the scene.
        [[nodiscard]] Admission TryBegin(const Managers::CookedCpuClosureCache::Lease&, std::span<const uuids::uuid>, const Cook::CookAssetIdentity&, Core::Memory::IAllocationRoute&, Core::Memory::TLSFSlab&, RenderResourceManager&, Hardwares::VulkanDevice&, RenderResourceManager::SceneResourcePreparationId, uint32_t max_textures, Ticket* output, bool pause_after_mesh_record = false);
        /// Producer observation of a recorded mesh upload. The optional test
        /// pause retains it until cancellation so the boundary is deterministic.
        [[nodiscard]] bool      MeshUploadRecorded(Ticket) const noexcept;
        /// Producer-only cancellation request. A concurrent submission may win;
        /// Poll supplies the terminal outcome.
        [[nodiscard]] bool      Cancel(Ticket) noexcept;
        /// Producer-only; a submitted result can still be waiting for renderer
        /// pipeline/environment readiness in the scene resource mailbox.
        [[nodiscard]] bool      Poll(Ticket, Outcome* output) const noexcept;
        /// Producer-only; only a terminal result can be acknowledged.
        [[nodiscard]] bool      Acknowledge(Ticket) noexcept;
        void                    Seal() noexcept
        {
            m_sealed.store(true, std::memory_order_release);
        }
        /// After producer threads have joined, request cancellation for all work.
        void                                         CancelAll() noexcept;
        /// Joined-owner shutdown only, after sealing/joining producers and the
        /// render thread, and after device idle or terminal device loss. No
        /// command recorded before this call may be submitted afterwards.
        [[nodiscard]] bool                           DrainAfterDeviceIdle() noexcept;
        /// Render-owner only, one bounded operation for one request per call.
        [[nodiscard]] bool                           PumpOne(uint8_t frame_index, uint8_t thread_index);
        /// Render-owner only, borrow until the next PumpOne on this mailbox.
        [[nodiscard]] Textures::CookedTextureUpload* FindTexture(Ticket, const Cook::CookAssetIdentity&) noexcept;
        /// Terminal results may remain unacknowledged; no driver is still live.
        [[nodiscard]] bool                           Drained() const noexcept;

    private:
        enum class Phase : uint8_t
        {
            Free,
            Writing,
            Queued,
            Running,
            Finished
        };
        struct Slot
        {
            std::atomic<Phase>                                PhaseValue{Phase::Free};
            std::atomic<bool>                                 Cancelled{false};
            std::atomic<bool>                                 MeshRecorded{false};
            bool                                              PauseAfterMeshRecord = false;
            uint64_t                                          Serial               = 0;
            RenderResourceManager::SceneResourcePreparationId Scene                = {};
            Outcome                                           Result               = Outcome::Pending;
            CookedSceneGpuPreparation                         Driver               = {};
        };
        Slot              m_slots[Capacity] = {};
        std::atomic<bool> m_sealed{false};
        uint64_t          m_next_serial = 0;
        uint32_t          m_turn        = 0;
    };
} // namespace ZEngine::Rendering

#pragma once
#include <ZEngine/Core/Containers/SPSCQueue.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <uuid.h>
#include <cstdint>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering
{
    class RenderResourceManager;

    /// @brief Per-mesh load request submitted to the streaming manager.
    struct StreamRequest
    {
        uuids::uuid           UUID     = {}; ///< Asset UUID; used for dedup and UUID map update.
        Managers::AssetHandle Asset    = 0;  ///< Asset handle to read geometry data from.
        BufferHandle          Handle   = {}; ///< Pre-allocated mesh slot — manager fills its region.
        uint8_t               Priority = 0;  ///< 0 = high (currently visible), 1 = prefetch.
    };

    /// @brief Drives streaming geometry eviction on behalf of RenderResourceManager.
    /// @details Tick() is called once per frame from RenderResourceManager::BeginFrame, after
    ///          batch stagings are retired and pending uploads are flushed.
    ///
    ///          Eviction uses a clock-hand sweep over the mesh slot array: slots with Referenced=true
    ///          get one grace cycle (bit cleared, slot skipped); slots with Referenced=false are
    ///          evicted when pool usage exceeds kEvictionThreshold. The Referenced bits are cleared
    ///          at the end of every Tick so RenderScene can mark fresh references each frame.
    class GeometryStreamingManager
    {
    public:
        /// @brief Pool usage (vtx or idx axis) above which the eviction sweep runs.
        static constexpr float kEvictionThreshold   = 0.85f;
        /// @brief Fragmentation ratio above which a compaction is requested.
        static constexpr float kCompactionThreshold = 0.30f;

        /// @brief Bind the manager to a device and RRM. Must be called before Tick().
        void                   Initialize(Hardwares::VulkanDevice* device, RenderResourceManager* rrm);
        void                   Deinitialize();

        /// @brief Per-frame driver — called from RenderResourceManager::BeginFrame.
        /// @details Checks pool fragmentation, runs the eviction sweep if pool is under
        ///          pressure, then clears all Referenced bits for the coming frame.
        /// @param frame_index Current swapchain frame index.
        void                   Tick(uint32_t frame_index);

        /// @brief Enqueue a mesh load request. Single-producer (render thread), consumed by Tick().
        /// @details Pushes onto m_load_queue (SPSC). Returns false and drops the request if
        ///          the queue is full (kLoadQueueCapacity requests already pending).
        bool                   RequestLoad(const StreamRequest& req);

        /// @brief Mark a mesh slot for eviction. Render-thread only (future use).
        void                   RequestEvict(BufferHandle handle);

        /// @brief True if a compaction pass has been requested and not yet serviced.
        bool                   IsCompactionRequested() const
        {
            return m_compact_requested;
        }

        /// @brief Acknowledge and clear the compaction request after the caller has compacted.
        void ClearCompactionRequest()
        {
            m_compact_requested = false;
        }

    private:
        void                                                           RunEvictionSweep();
        void                                                           DrainLoadQueue(uint32_t frame_index);

        static constexpr uint32_t                                      kLoadQueueCapacity  = 256;

        Hardwares::VulkanDevice*                                       m_device            = nullptr;
        RenderResourceManager*                                         m_rrm               = nullptr;
        Core::Containers::SPSCQueue<StreamRequest, kLoadQueueCapacity> m_load_queue        = {};
        uint32_t                                                       m_clock_hand        = 0;
        bool                                                           m_compact_requested = false;
    };

} // namespace ZEngine::Rendering

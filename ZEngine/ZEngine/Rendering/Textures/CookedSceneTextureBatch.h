#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Rendering/Textures/CookedTextureUpload.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Textures
{
    /// Transaction for the texture portion of a cooked scene. Initialize on an
    /// exclusive worker, then transfer ownership to the render owner for Step.
    /// One Step performs at most sixteen 64 KiB staging writes, stopping after
    /// a 1 ms deadline between writes, or performs one admission.
    /// The route, cache, manager and device outlive this owner. The host keeps
    /// rendering/processing graph acquire submissions while Pending.
    class CookedSceneTextureBatch final
    {
    public:
        enum class State : uint8_t
        {
            Empty,
            Pending,
            Ready,
            Failed
        };
        CookedSceneTextureBatch() = default;
        ~CookedSceneTextureBatch();
        CookedSceneTextureBatch(const CookedSceneTextureBatch&)                      = delete;
        CookedSceneTextureBatch&           operator=(const CookedSceneTextureBatch&) = delete;

        /// Consumes the ready closure lease only after bounded allocation and
        /// complete admission. No scene or output state changes on failure.
        [[nodiscard]] bool                 Initialize(Managers::CookedCpuClosureCache::Lease&&, Core::Memory::IAllocationRoute&, RenderResourceManager&, Hardwares::VulkanDevice&, RenderResourceManager::SceneResourcePreparationId, uint32_t max_textures);
        [[nodiscard]] State                Step(uint8_t frame_index, uint8_t thread_index);
        /// After textures are ready, retain one closure material per call in
        /// the same scene request. Non-material entries also consume a step.
        /// Pending includes texture acquisition and descriptor backpressure.
        [[nodiscard]] State                StepMaterials();
        /// Borrow for material preparation while this batch remains alive.
        [[nodiscard]] CookedTextureUpload* Find(const Cook::CookAssetIdentity&) noexcept;
        [[nodiscard]] State                Status() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] State MaterialStatus() const noexcept
        {
            return m_material_state;
        }
        /// Releases untransferred destinations through normal GPU retirement.
        /// The scene owns successfully transferred destinations. The caller must
        /// cancel/release the scene request after failure or cancellation.
        void Reset() noexcept;

    private:
        struct Entry;
        Core::Memory::IAllocationRoute*                   m_route           = nullptr;
        RenderResourceManager*                            m_manager         = nullptr;
        Hardwares::VulkanDevice*                          m_device          = nullptr;
        Managers::CookedCpuClosureCache::Lease            m_lease           = {};
        RenderResourceManager::SceneResourcePreparationId m_scene           = {};
        Entry*                                            m_entries         = nullptr;
        uint32_t                                          m_count           = 0;
        uint32_t                                          m_submit_cursor   = 0;
        uint32_t                                          m_transfer_cursor = 0;
        size_t                                            m_material_cursor = 0;
        uint64_t                                          m_staging_visits  = 0;
        uint64_t                                          m_staging_writes  = 0;
        uint64_t                                          m_staging_max_ns  = 0;
        State                                             m_material_state  = State::Pending;
        State                                             m_state           = State::Empty;
    };
} // namespace ZEngine::Rendering::Textures

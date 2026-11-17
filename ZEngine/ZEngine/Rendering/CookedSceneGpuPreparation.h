#pragma once
#include <ZEngine/Rendering/Meshes/CookedSceneMeshBatch.h>
#include <ZEngine/Rendering/Textures/CookedSceneEnvironmentBatch.h>
#include <ZEngine/Rendering/Textures/CookedSceneTextureBatch.h>

namespace ZEngine::Rendering
{
    /// Exclusive request driver. Initialize on a worker, then hand off to the
    /// render owner. The caller keeps the cache, route, metadata slab, manager
    /// and device alive and continues frame submissions while Step is pending.
    class CookedSceneGpuPreparation final
    {
    public:
        enum class State : uint8_t
        {
            Empty,
            Pending,
            Draining,
            Submitted,
            Failed,
            Cancelled
        };
        CookedSceneGpuPreparation() = default;
        ~CookedSceneGpuPreparation();
        CookedSceneGpuPreparation(const CookedSceneGpuPreparation&)            = delete;
        CookedSceneGpuPreparation& operator=(const CookedSceneGpuPreparation&) = delete;

        /// The caller retains its closure lease. Failure preserves it and the
        /// scene request; no GPU work or scene resource transfer has begun.
        [[nodiscard]] bool         Initialize(const Managers::CookedCpuClosureCache::Lease&, std::span<const uuids::uuid> meshes, const Cook::CookAssetIdentity& environment, Core::Memory::IAllocationRoute&, Core::Memory::TLSFSlab&, RenderResourceManager&, Hardwares::VulkanDevice&, RenderResourceManager::SceneResourcePreparationId, uint32_t max_textures);
        /// Submitted means all resources were handed to the scene mailbox.
        /// It is not scene/pipeline/sky readiness; poll the mailbox separately.
        [[nodiscard]] State        Step(uint8_t frame_index, uint8_t thread_index);
        /// Cancels the scene request and releases untransferred uploads. Drain
        /// accepted mesh work through Step before Reset or destruction.
        void                       Cancel() noexcept;
        /// Joined-owner shutdown only, after device idle or terminal loss and
        /// after preventing all further submissions.
        [[nodiscard]] bool         AbandonAfterDeviceIdle() noexcept;
        [[nodiscard]] bool         Reset() noexcept;
        [[nodiscard]] State        Status() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] bool HasRecordedMeshUpload() const noexcept
        {
            return m_meshes.HasRecordedUpload();
        }
        [[nodiscard]] Textures::CookedTextureUpload* FindTexture(const Cook::CookAssetIdentity& identity) noexcept
        {
            return m_textures.Find(identity);
        }

    private:
        void                                              Stop(State outcome) noexcept;
        RenderResourceManager*                            m_manager         = nullptr;
        RenderResourceManager::SceneResourcePreparationId m_scene           = {};
        Meshes::CookedSceneMeshBatch                      m_meshes          = {};
        Textures::CookedSceneTextureBatch                 m_textures        = {};
        Textures::CookedSceneEnvironmentBatch             m_environment     = {};
        bool                                              m_has_environment = false;
        uint32_t                                          m_turn            = 0;
        State                                             m_state           = State::Empty;
        State                                             m_after_drain     = State::Cancelled;
    };
} // namespace ZEngine::Rendering

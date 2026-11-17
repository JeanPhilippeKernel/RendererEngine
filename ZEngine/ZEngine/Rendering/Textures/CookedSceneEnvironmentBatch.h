#pragma once
#include <ZEngine/Rendering/Textures/CookedEnvironmentUpload.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Textures
{
    /// One cooked HDRI upload for a scene request. Initialize on an exclusive
    /// worker, then transfer to the render owner for bounded Step calls. The
    /// cache, manager and device outlive this object.
    class CookedSceneEnvironmentBatch final
    {
    public:
        enum class State : uint8_t
        {
            Empty,
            Pending,
            Ready,
            Failed
        };
        CookedSceneEnvironmentBatch() = default;
        ~CookedSceneEnvironmentBatch();
        CookedSceneEnvironmentBatch(const CookedSceneEnvironmentBatch&)            = delete;
        CookedSceneEnvironmentBatch& operator=(const CookedSceneEnvironmentBatch&) = delete;
        /// Failure preserves the lease and leaves this owner empty. transfer_source
        /// is optional and permits an explicit GPU readback/transfer consumer.
        [[nodiscard]] bool           Initialize(Managers::CookedCpuClosureCache::Lease&&, const Cook::CookAssetIdentity&, RenderResourceManager&, Hardwares::VulkanDevice&, RenderResourceManager::SceneResourcePreparationId, bool transfer_source = false);
        /// Ready means the scene owns the submitted source, not that the GPU
        /// upload or sky bake has completed.
        [[nodiscard]] State          Step(uint8_t frame_index, uint8_t thread_index);
        [[nodiscard]] State          Status() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] TextureHandle Texture() const noexcept
        {
            return m_target;
        }
        /// Untransferred targets retire through the resource manager. The scene
        /// owns a transferred target and must be released separately on failure.
        void Reset() noexcept;

    private:
        Managers::CookedCpuClosureCache::Lease            m_lease           = {};
        CookedEnvironmentUpload                           m_upload          = {};
        RenderResourceManager*                            m_manager         = nullptr;
        Hardwares::VulkanDevice*                          m_device          = nullptr;
        RenderResourceManager::SceneResourcePreparationId m_scene           = {};
        Cook::CookAssetIdentity                           m_identity        = {};
        TextureHandle                                     m_target          = {};
        bool                                              m_transfer_source = false;
        bool                                              m_transferred     = false;
        State                                             m_state           = State::Empty;
    };
} // namespace ZEngine::Rendering::Textures

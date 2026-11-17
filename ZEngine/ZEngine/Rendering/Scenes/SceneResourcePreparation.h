#pragma once
#include <ZEngine/Rendering/RenderResourceManager.h>

namespace ZEngine::ECS
{
    class SceneInstance;
}
namespace ZEngine::Managers
{
    class CookedCpuAssetSet;
}

namespace ZEngine::Rendering::Scenes
{
    /// Owns one scene's retained GPU-resource preparation. The scene instance,
    /// render adapter and resource manager must outlive this owner.
    class SceneResourcePreparation final
    {
    public:
        ~SceneResourcePreparation();
        SceneResourcePreparation()                                                                                 = default;
        SceneResourcePreparation(const SceneResourcePreparation&)                                                  = delete;
        SceneResourcePreparation&                                       operator=(const SceneResourcePreparation&) = delete;

        [[nodiscard]] bool                                              Prepare(ECS::SceneInstance&, RenderScene&, std::span<const uuids::uuid>, RenderResourceManager&, ECS::SceneRequestId, uint64_t, const SceneInitialView&, const Managers::CookedCpuAssetSet*, bool* terminal_failure);
        [[nodiscard]] bool                                              Poll(RenderResourceManager::SceneResourcePreparationResult*) const;
        [[nodiscard]] RenderResourceManager::SceneResourcePreparationId InitialResources();
        /// @brief Report whether all expected instances have been collected.
        [[nodiscard]] bool                                              InstancesCollected(size_t count) const noexcept;
        /// @brief Report whether a resource preparation is active.
        [[nodiscard]] bool                                              HasPreparation() const noexcept;

    private:
        RenderResourceManager*                            m_resources        = nullptr;
        RenderResourceManager::SceneResourcePreparationId m_preparation      = {};
        uint32_t                                          m_instances_copied = 0;
        bool                                              m_cooked           = false;
    };
} // namespace ZEngine::Rendering::Scenes

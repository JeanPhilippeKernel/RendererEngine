#pragma once
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Rendering/Scenes/SceneMeshBindings.h>
#include <ZEngine/Rendering/Scenes/SceneResourcePreparation.h>

namespace ZEngine::Managers
{
    class CookedCpuAssetSet;
}

namespace ZEngine::Rendering::Scenes
{
    /// @brief Owns an unpublished or active renderer-only scene and its bindings.
    /// @details Scene storage outlives the render adapter and retained resources.
    /// Retire this owner before the resource manager and cache dependencies shut down.
    class RuntimeScene final
    {
    public:
        ~RuntimeScene()                                                           = default;
        RuntimeScene(const RuntimeScene&)                                         = delete;
        RuntimeScene&                              operator=(const RuntimeScene&) = delete;

        /// @brief Build render bindings from a verified cooked asset closure.
        /// @details Success consumes the instance. Validation failure preserves it;
        /// failures after binding starts consume and discard it.
        [[nodiscard]] static bool                  PrepareFromCooked(std::unique_ptr<ECS::SceneInstance>&&, const Managers::CookedCpuAssetSet&, std::unique_ptr<RuntimeScene>*, ECS::SceneDiagnostics*, const SceneMeshBindingLimits& = {});
        /// @brief Return the owned scene instance.
        [[nodiscard]] ECS::SceneInstance&          Instance() noexcept;
        /// @brief Return the scene's render adapter.
        [[nodiscard]] RenderScene&                 Render() noexcept;
        /// @brief Return the owner of the scene's prepared render resources.
        [[nodiscard]] SceneResourcePreparation&    Resources() noexcept;
        /// @brief Return the mesh identities needed by the render bindings.
        [[nodiscard]] std::span<const uuids::uuid> RequiredMeshes() const noexcept;
        /// @brief Return the number of prepared draw instances.
        [[nodiscard]] uint32_t                     DrawCount() const noexcept;

    private:
        RuntimeScene()                                    = default;
        std::unique_ptr<ECS::SceneInstance>  m_instance   = {};
        RenderScene                          m_render     = {};
        Core::Containers::Array<uuids::uuid> m_meshes     = {};
        uint32_t                             m_draw_count = 0;
        SceneResourcePreparation             m_resources  = {};
    };
} // namespace ZEngine::Rendering::Scenes

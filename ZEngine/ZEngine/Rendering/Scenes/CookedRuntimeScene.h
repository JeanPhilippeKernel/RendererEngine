#pragma once
#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/Rendering/Scenes/SceneMeshBindings.h>
#include <ZEngine/Rendering/Scenes/SceneResourcePreparation.h>

namespace ZEngine::Rendering::Scenes
{
    /// Unpublished or active renderer-only world. Its scene storage outlives
    /// the render adapter and retained resources; callers must retire it before
    /// the resource manager and its cache dependencies shut down.
    class CookedRuntimeScene final
    {
    public:
        ~CookedRuntimeScene()                                                  = default;
        CookedRuntimeScene(const CookedRuntimeScene&)                          = delete;
        CookedRuntimeScene&               operator=(const CookedRuntimeScene&) = delete;

        /// Success consumes the instance. Validation failure preserves it;
        /// failures after binding starts consume and discard it.
        [[nodiscard]] static bool         Prepare(std::unique_ptr<ECS::SceneInstance>&&, const Managers::CookedCpuAssetSet&, std::unique_ptr<CookedRuntimeScene>*, ECS::SceneDiagnostics*, const SceneMeshBindingLimits& = {});
        [[nodiscard]] ECS::SceneInstance& Instance() noexcept
        {
            return *m_instance;
        }
        [[nodiscard]] RenderScene& Render() noexcept
        {
            return m_render;
        }
        [[nodiscard]] SceneResourcePreparation& Resources() noexcept
        {
            return m_resources;
        }
        [[nodiscard]] std::span<const uuids::uuid> RequiredMeshes() const noexcept
        {
            return {m_meshes.data(), m_meshes.size()};
        }
        [[nodiscard]] uint32_t DrawCount() const noexcept
        {
            return m_draw_count;
        }

    private:
        CookedRuntimeScene()                              = default;
        std::unique_ptr<ECS::SceneInstance>  m_instance   = {};
        RenderScene                          m_render     = {};
        Core::Containers::Array<uuids::uuid> m_meshes     = {};
        uint32_t                             m_draw_count = 0;
        SceneResourcePreparation             m_resources  = {};
    };
} // namespace ZEngine::Rendering::Scenes

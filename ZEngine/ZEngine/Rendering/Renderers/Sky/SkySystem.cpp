#include <ZEngine/Rendering/Renderers/Sky/SkySystem.h>

namespace ZEngine::Rendering::Renderers
{
    void SkySystem::Initialize(Hardwares::VulkanDevicePtr device, RenderGraph* graph)
    {
        m_graph = graph;

        // Register all sky backends. The graph will compile all of them; only one is enabled
        // per frame based on the active SkyMode. All start disabled; ApplyMode enables the
        // correct one. The graph is recompiled via Compile() on the first mode change.
        graph->AddCallbackPass("Sky Sphere Pass", &m_skysphere_pass, true);

        // Disable the legacy skybox pass — it is replaced by the new sky system.
        graph->SetPassEnabled("Skybox Pass", false);
    }

    void SkySystem::SetConfig(const Sky::SkyConfig& cfg)
    {
        m_mode_changed          = (cfg.Mode != m_config.Mode);
        m_config                = cfg;

        m_skysphere_pass.Config = cfg;

        ApplyMode();
    }

    void SkySystem::Dispose()
    {
        m_graph = nullptr;
    }

    void SkySystem::ApplyMode()
    {
        if (!m_graph)
            return;

        m_graph->SetPassEnabled("Sky Sphere Pass", m_config.Mode == Sky::SkyMode::SkySphere);
    }
} // namespace ZEngine::Rendering::Renderers

#include <ZEngine/Rendering/Renderers/Sky/SkySystem.h>

namespace ZEngine::Rendering::Renderers
{
    void SkySystem::Initialize(Hardwares::VulkanDevicePtr device, RenderGraph* graph)
    {
        m_graph = graph;

        // Register all sky backends. The graph compiles all of them at startup; ApplyMode
        // enables exactly one per frame. See issue #779 for runtime recompilation design.
        graph->AddCallbackPass("Sky Atmosphere Pass", &m_atmosphere_pass, true);
        graph->AddCallbackPass("Sky Sphere Pass", &m_skysphere_pass, false);

        // Disable the legacy skybox pass — replaced by the new sky system.
        graph->SetPassEnabled("Skybox Pass", false);
    }

    void SkySystem::SetConfig(const Sky::SkyConfig& cfg)
    {
        m_mode_changed              = (cfg.Mode != m_config.Mode);
        m_config                    = cfg;

        m_atmosphere_pass.Config    = cfg;
        m_atmosphere_pass.LUTsDirty = m_mode_changed || m_atmosphere_pass.LUTsDirty;
        m_skysphere_pass.Config     = cfg;

        ApplyMode();
    }

    void SkySystem::Dispose(Hardwares::VulkanDevicePtr device)
    {
        m_atmosphere_pass.Deinitialize(device);
        m_graph = nullptr;
    }

    void SkySystem::ApplyMode()
    {
        if (!m_graph)
            return;

        m_graph->SetPassEnabled("Sky Atmosphere Pass", m_config.Mode == Sky::SkyMode::Atmosphere);
        m_graph->SetPassEnabled("Sky Sphere Pass", m_config.Mode == Sky::SkyMode::SkySphere);
    }
} // namespace ZEngine::Rendering::Renderers

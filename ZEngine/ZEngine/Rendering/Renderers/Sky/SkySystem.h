#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Renderers/Sky/SkyAtmospherePass.h>
#include <ZEngine/Rendering/Renderers/Sky/SkySpherePass.h>
#include <ZEngine/Rendering/Sky/SkyConfig.h>

namespace ZEngine::Rendering::Renderers
{
    struct SkySystem
    {
        void         Initialize(Hardwares::VulkanDevicePtr device, RenderGraph* graph);
        void         SetConfig(const Sky::SkyConfig& cfg);
        void         SubmitLUTs(Hardwares::VulkanDevicePtr device, Rendering::Scenes::SceneDataPtr const scene);
        void         UpdateSunDirection(const Core::Maths::Vec4f& direction);
        void         Dispose(Hardwares::VulkanDevicePtr device);

        Sky::SkyMode GetActiveMode() const
        {
            return m_config.Mode;
        }

    private:
        void              ApplyMode();

        Sky::SkyConfig    m_config       = {};
        RenderGraph*      m_graph        = nullptr;
        bool              m_mode_changed = false;

        SkyAtmospherePass m_atmosphere_pass;
        SkySpherePass     m_skysphere_pass;
    };
} // namespace ZEngine::Rendering::Renderers

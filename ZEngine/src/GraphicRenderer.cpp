#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Window/GlfwWindow/VulkanWindow.h>

namespace ZEngine::Rendering::Renderers
{
    GraphicRenderer::GraphicRenderer()
    {
        m_renderer_information = CreateRef<GraphicRendererInformation>();
    }

    GraphicRenderer::~GraphicRenderer()
    {
    }

    void GraphicRenderer::StartScene(const Contracts::GraphicSceneLayout& scene_data)
    {
        m_scene_data = scene_data;
    }

    const Ref<GraphicRendererInformation>& GraphicRenderer::GetRendererInformation() const
    {
        return m_renderer_information;
    }
} // namespace ZEngine::Rendering::Renderers

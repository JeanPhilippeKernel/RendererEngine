#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers
{

    class GraphicRenderer3D : public GraphicRenderer
    {
    public:
        GraphicRenderer3D();
        virtual ~GraphicRenderer3D();

        void Initialize() override;
        void Deinitialize() override;

        virtual void StartScene(const Contracts::GraphicSceneLayout& scene_data) override;
        virtual void EndScene() override;
        virtual void RequestOutputImageSize(uint32_t width = 1, uint32_t height = 1) override;

        Contracts::FramebufferViewLayout GetOutputImage(uint32_t frame_index) const override;

    private:
        void MarkVulkanInternalObjectDirty();

    private:
        VkRenderPass                       m_renderpass{VK_NULL_HANDLE};
        Pipelines::StandardGraphicPipeline m_standard_pipeline;
    };
} // namespace ZEngine::Rendering::Renderers

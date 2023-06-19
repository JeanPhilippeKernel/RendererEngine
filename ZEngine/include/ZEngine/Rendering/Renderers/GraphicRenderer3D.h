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

    private:
        void MarkVulkanInternalObjectDirty(Hardwares::VulkanDevice& device);

    private:
        Pipelines::StandardGraphicPipeline        m_standard_pipeline;
        Buffers::VertexBuffer<std::vector<float>> m_vertex_buffer;
        Buffers::IndexBuffer<std::vector<float>>  m_index_buffer;
    };
} // namespace ZEngine::Rendering::Renderers

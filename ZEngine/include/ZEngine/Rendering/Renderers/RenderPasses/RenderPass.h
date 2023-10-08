#pragma once
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass
    {
        RenderPass() = default;
        ~RenderPass();

        void Dispose();

        Ref<Pipelines::GraphicPipeline> GetPipeline() const;
        void                            Bake();
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer);

        static Ref<RenderPass> Create(const RenderPassSpecification& specification);

    private:
        Ref<Pipelines::GraphicPipeline> m_pipeline;
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
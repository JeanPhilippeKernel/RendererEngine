#pragma once 
#include <string>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass
    {
        RenderPass() = default;
        ~RenderPass();

        void Dispose();

        Ref<Pipelines::GraphicPipeline> GetPipeline() const;
        void Bake();

        static Ref<RenderPass> Create(const RenderPassSpecification& specification);

    private:
        Ref<Pipelines::GraphicPipeline> m_pipeline;
    };
}
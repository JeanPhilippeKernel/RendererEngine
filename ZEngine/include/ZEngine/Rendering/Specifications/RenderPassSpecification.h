#pragma once
#include <ZEngineDef.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPassSpecification
    {
        std::string                     DebugName;
        Ref<Pipelines::GraphicPipeline> Pipeline;
    };
} // namespace ZEngine::Rendering::Specifications

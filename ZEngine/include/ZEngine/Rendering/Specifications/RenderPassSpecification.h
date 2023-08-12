#pragma once
#include <vector>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPassSpecification
    {
        std::string                     DebugName;
        Ref<Pipelines::GraphicPipeline> Pipeline;
    };
} // namespace ZEngine::Rendering::Specifications

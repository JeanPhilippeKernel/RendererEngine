#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Helpers
{
    void             FillDefaultPipelineFixedStates(Rendering::Specifications::GraphicRendererPipelineStateSpecification& specification);

} // namespace ZEngine::Helpers
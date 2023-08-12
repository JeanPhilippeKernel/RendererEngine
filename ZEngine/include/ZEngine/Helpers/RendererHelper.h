#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Helpers
{
    VkPipelineLayout CreatePipelineLayout(const VkPipelineLayoutCreateInfo& pipeline_layout_create_info);
    void             FillDefaultPipelineFixedStates(Rendering::Specifications::GraphicRendererPipelineStateSpecification& specification);

} // namespace ZEngine::Helpers
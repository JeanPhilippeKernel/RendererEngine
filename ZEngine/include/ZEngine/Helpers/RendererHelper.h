#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Helpers
{
    VkRenderPass     CreateRenderPass(Rendering::Renderers::RenderPasses::AttachmentSpecification& specification);
    VkPipelineLayout CreatePipelineLayout(const VkPipelineLayoutCreateInfo& pipeline_layout_create_info);
    void             FillDefaultPipelineFixedStates(Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification);

} // namespace ZEngine::Helpers
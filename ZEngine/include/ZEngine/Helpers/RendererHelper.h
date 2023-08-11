#pragma once
#include <Hardwares/VulkanDevice.h>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Helpers
{
    VkRenderPass     CreateRenderPass(Rendering::Renderers::RenderPasses::AttachmentSpecification& specification);
    VkPipelineLayout CreatePipelineLayout(const VkPipelineLayoutCreateInfo& pipeline_layout_create_info);
    VkFramebuffer    CreateFramebuffer(
           const std::vector<VkImageView>& attachments,
           const VkRenderPass&             render_pass,
           uint32_t                        width,
           uint32_t                        height,
           uint32_t                        layer_number = 1);
    VkSampler   CreateTextureSampler();
    void        FillDefaultPipelineFixedStates(Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification);
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
    VkFormat FindDepthFormat();
} // namespace ZEngine::Helpers
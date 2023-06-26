#pragma once
#include <Hardwares/VulkanDevice.h>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Helpers
{
    VkRenderPass     CreateRenderPass(Hardwares::VulkanDevice& device, Rendering::Renderers::RenderPasses::RenderPassSpecification& specification);
    VkPipelineLayout CreatePipelineLayout(Hardwares::VulkanDevice& device, const VkPipelineLayoutCreateInfo& pipeline_layout_create_info);
    VkPipeline       CreateGraphicPipeline(
              Hardwares::VulkanDevice&                                                          device,
              VkPipelineLayout&                                                                 pipeline_layout,
              VkRenderPass&                                                                     render_pass,
              const Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification);
    VkFramebuffer CreateFramebuffer(
        Hardwares::VulkanDevice& device,
        std::vector<VkImageView> image_view_attachments,
        const VkRenderPass&      render_pass,
        const VkExtent2D&        swapchain_extent,
        uint32_t                 layer_number = 1);
    VkImage CreateImage(
        Hardwares::VulkanDevice&                device,
        uint32_t                                width,
        uint32_t                                height,
        VkImageType                             image_type,
        VkFormat                                image_format,
        VkImageTiling                           image_tiling,
        VkImageLayout                           image_initial_layout,
        VkImageUsageFlags                       image_usage,
        VkSharingMode                           image_sharing_mode,
        VkSampleCountFlagBits                   image_sample_count,
        VkDeviceMemory&                         device_memory,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkMemoryPropertyFlags                   requested_properties);
    VkImageView CreateImageView(Hardwares::VulkanDevice& device, VkImage image, VkFormat image_format, VkImageAspectFlagBits image_aspect_flag);
    VkSampler   CreateTextureSampler(Hardwares::VulkanDevice& device);
    void        FillDefaultPipelineFixedStates(Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification, const VkExtent2D& swapchain_extent);
    void        TransitionImageLayout(Hardwares::VulkanDevice& device, VkImage image, VkFormat image_format, VkImageLayout old_image_layout, VkImageLayout new_image_layout);
    VkFormat FindSupportedFormat(Hardwares::VulkanDevice& device, const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
    VkFormat FindDepthFormat(Hardwares::VulkanDevice& device);
    /*Build-in pipelines*/
    Rendering::Renderers::Pipelines::StandardGraphicPipeline CreateStandardGraphicPipeline(
        Hardwares::VulkanDevice& device,
        const VkExtent2D&        extent,
        VkRenderPass             render_pass,
        uint32_t                 frame_count);

} // namespace ZEngine::Helpers
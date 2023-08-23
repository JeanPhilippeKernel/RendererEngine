#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct GraphicRendererPipelineStateSpecification
    {
        VkViewport                              Viewport;
        VkRect2D                                Scissor;
        std::vector<VkDynamicState>             DynamicStates;
        VkPipelineDynamicStateCreateInfo        DynamicStateCreateInfo;
        VkPipelineVertexInputStateCreateInfo    VertexInputStateCreateInfo;
        VkPipelineInputAssemblyStateCreateInfo  InputAssemblyStateCreateInfo;
        VkPipelineViewportStateCreateInfo       ViewportStateCreateInfo;
        VkPipelineRasterizationStateCreateInfo  RasterizationStateCreateInfo;
        VkPipelineMultisampleStateCreateInfo    MultisampleStateCreateInfo;
        VkPipelineColorBlendAttachmentState     ColorBlendAttachmentState;
        VkPipelineColorBlendStateCreateInfo     ColorBlendStateCreateInfo;
        VkPipelineLayoutCreateInfo              LayoutCreateInfo;
        VkPipelineDepthStencilStateCreateInfo   DepthStencilStateCreateInfo;
    };

    struct GraphicRendererPipelineSpecification
    {
        GraphicRendererPipelineSpecification() = default;

        std::string                                       DebugName;
        std::string                                       VertexShaderFilename;
        std::string                                       FragmentShaderFilename;
        std::vector<VkDescriptorSetLayoutBinding>         LayoutBindingCollection;
        std::vector<VkDescriptorSetLayout>                DescriptorSetLayoutCollection;
        FrameBufferSpecificationVNext TargetFrameBufferSpecification;
        GraphicRendererPipelineStateSpecification         StateSpecification;
    };
}
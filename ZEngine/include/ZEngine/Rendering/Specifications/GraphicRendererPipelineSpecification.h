#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <ZEngineDef.h>

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

        std::string                               DebugName;
        ShaderSpecification                       ShaderSpecification;
        Ref<Rendering::Buffers::FramebufferVNext> TargetFrameBufferSpecification;
        GraphicRendererPipelineStateSpecification StateSpecification;
    };
}
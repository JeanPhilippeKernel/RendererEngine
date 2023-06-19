#pragma once
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Shaders/ShaderInformation.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicRendererPipelineStateSpecification
    {
        std::vector<VkDynamicState>             DynamicStates;
        VkViewport                              Viewport;
        VkRect2D                                Scissor;
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
        std::vector<Shaders::ShaderInformation> ShaderInfo;
    };

    struct GraphicRendererPipelineSpecification
    {
        GraphicRendererPipelineSpecification() = default;

        RenderPasses::RenderPassSpecification     RenderPassSpecification;
        GraphicRendererPipelineStateSpecification StateSpecification;
    };
}
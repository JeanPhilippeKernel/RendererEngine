#pragma once
#include <Rendering/Shaders/ShaderInformation.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

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

        std::string                                       DebugName;
        std::string                                       VertexShaderFilename;
        std::string                                       FragmentShaderFilename;
        std::vector<VkDescriptorSetLayoutBinding>         LayoutBindingCollection;
        std::vector<VkDescriptorSetLayout>                DescriptorSetLayoutCollection;
        Rendering::Buffers::FrameBufferSpecificationVNext TargetFrameBufferSpecification;
        GraphicRendererPipelineStateSpecification         StateSpecification;
    };
}
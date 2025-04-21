#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline
    {
    public:
        GraphicPipeline() {}
        ~GraphicPipeline() {}

        Specifications::GraphicRendererPipelineSpecification Specification = {};
        Shaders::Shader*                                     Shader        = nullptr;
        Hardwares::VulkanDevice*                             Device        = nullptr;
        VkPipeline                                           Handle        = VK_NULL_HANDLE;
        VkPipelineLayout                                     Layout        = VK_NULL_HANDLE;

        void                                                 Initialize(Hardwares::VulkanDevice* device, Specifications::GraphicRendererPipelineSpecification&& spec);
        void                                                 Bake();
        void                                                 Dispose();
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
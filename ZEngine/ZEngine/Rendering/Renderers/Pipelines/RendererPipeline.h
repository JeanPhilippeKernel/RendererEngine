#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <ZEngine/ZEngineDef.h>
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
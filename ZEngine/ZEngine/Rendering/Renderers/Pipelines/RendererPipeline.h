#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct IPipeline
    {
        virtual ~IPipeline()                             = default;

        Shaders::Shader*            Shader               = nullptr;
        Hardwares::VulkanDevice*    Device               = nullptr;
        VkPipeline                  Handle               = VK_NULL_HANDLE;
        VkPipelineLayout            Layout               = VK_NULL_HANDLE;

        virtual VkPipelineBindPoint GetBindPoint() const = 0;
        virtual void                Bake()               = 0;
        virtual void                Dispose()            = 0;
    };

    struct GraphicPipeline : IPipeline
    {
        Specifications::GraphicRendererPipelineSpecification Specification = {};

        VkPipelineBindPoint                                  GetBindPoint() const override
        {
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        }
        void Initialize(Hardwares::VulkanDevice* device, Specifications::GraphicRendererPipelineSpecification&& spec);
        void Bake() override;
        void Dispose() override;
    };

    struct ComputePipeline : IPipeline
    {
        VkPipelineBindPoint GetBindPoint() const override
        {
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        }
        void Initialize(Hardwares::VulkanDevice* device, cstring shader_name, uint32_t push_constant_size = 0);
        void Bake() override;
        void Dispose() override;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines

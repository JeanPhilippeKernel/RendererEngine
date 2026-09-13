#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Specifications/GraphicsPipelineDesc.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct IPipeline
    {
        using DescriptorReplayCallback                      = bool (*)(void* context);

        virtual ~IPipeline()                                = default;

        Shaders::Shader*            Shader                  = nullptr;
        Hardwares::VulkanDevice*    Device                  = nullptr;
        VkPipeline                  Handle                  = VK_NULL_HANDLE;
        VkPipelineLayout            Layout                  = VK_NULL_HANDLE;
        uint32_t                    BakedShaderGeneration   = UINT32_MAX;
        DescriptorReplayCallback    ReplayDescriptors       = nullptr;
        void*                       DescriptorReplayContext = nullptr;
        bool                        DescriptorBindingsValid = true;

        virtual VkPipelineBindPoint GetBindPoint() const    = 0;
        virtual void                Bake()                  = 0;
        virtual void                Dispose()               = 0;

        /// @brief Rebuilds this borrowed pipeline after a shader generation change.
        bool                        EnsureCurrent()
        {
            if (Shader && (Handle == VK_NULL_HANDLE || BakedShaderGeneration != Shader->Generation))
            {
                Bake();
                DescriptorBindingsValid = Handle != VK_NULL_HANDLE && (!ReplayDescriptors || ReplayDescriptors(DescriptorReplayContext));
            }
            return Handle != VK_NULL_HANDLE && DescriptorBindingsValid;
        }
    };

    struct GraphicPipeline : IPipeline
    {
        Specifications::GraphicsPipelineDesc Description = {};
        RenderPasses::Attachment*            Attachment  = nullptr;

        VkPipelineBindPoint                  GetBindPoint() const override
        {
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        }
        void Initialize(Hardwares::VulkanDevice* device, Specifications::GraphicsPipelineDesc&& desc, RenderPasses::Attachment* attachment);
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

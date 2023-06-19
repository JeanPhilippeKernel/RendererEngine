#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Buffers/UniformBuffer.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct StandardGraphicPipeline
    {
        VkRenderPass                                                    RenderPass{VK_NULL_HANDLE};
        VkPipelineLayout                                                PipelineLayout{VK_NULL_HANDLE};
        VkPipeline                                                      Pipeline{VK_NULL_HANDLE};
        std::vector<VkDescriptorSetLayoutBinding>                       DescriptorSetLayoutBindingCollection;
        VkDescriptorSetLayout                                           DescriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool                                                DescriptorPool{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet>                                    DescriptorSetCollection;
        std::vector<Buffers::UniformBuffer<Contracts::UBOCameraLayout>> UBOCameraPropertiesCollection;
        std::vector<Buffers::UniformBuffer<Contracts::UBOModelLayout>>  UBOModelPropertiesCollection;

        void Dispose();
    };

} // namespace ZEngine::Rendering::Renderers::Pipelines
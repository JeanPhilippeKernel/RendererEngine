#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct StandardGraphicPipeline
    {
        //VkRenderPass     RenderPass{VK_NULL_HANDLE};
        uint32_t         FrameCount;
        VkPipelineLayout PipelineLayout{VK_NULL_HANDLE};
        VkPipeline       Pipeline{VK_NULL_HANDLE};
        /*Shader stage info*/
        std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayoutBindingCollection;
        VkDescriptorSetLayout                     DescriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool                          DescriptorPool{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet>              DescriptorSetCollection;
        /*Fragment stage info*/
        std::vector<VkDescriptorSetLayoutBinding> FragmentDescriptorSetLayoutBindingCollection;
        VkDescriptorSetLayout                     FragmentDescriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool                          FragmentDescriptorPool{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet>              FragmentDescriptorSetCollection;
        /*Uniform Buffer info*/
        std::vector<Buffers::UniformBuffer<Contracts::UBOCameraLayout>> UBOCameraPropertiesCollection;
        std::vector<Buffers::UniformBuffer<Contracts::UBOModelLayout>>  UBOModelPropertiesCollection;
        /*Framebuffer info*/
        std::vector<Buffers::FramebufferVNext>            FramebufferCollection;
        Rendering::Buffers::FrameBufferSpecificationVNext FramebufferSpecification;
        void                                              Dispose();

        void CreateOrResizeFramebuffer(uint32_t width, uint32_t height);
        void UpdateDescriptorSet();
    };

} // namespace ZEngine::Rendering::Renderers::Pipelines
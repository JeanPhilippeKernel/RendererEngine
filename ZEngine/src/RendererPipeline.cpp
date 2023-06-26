#include <pch.h>
#include <Engine.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    void StandardGraphicPipeline::Dispose()
    {
        auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();

        {
            UBOCameraPropertiesCollection.clear();
            UBOCameraPropertiesCollection.shrink_to_fit();
            UBOModelPropertiesCollection.clear();
            UBOModelPropertiesCollection.shrink_to_fit();
        }

        if (DescriptorPool)
        {
            vkDestroyDescriptorPool(device_handle, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
        }
        if (DescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(device_handle, DescriptorSetLayout, nullptr);
            DescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (FragmentDescriptorPool)
        {
            vkDestroyDescriptorPool(device_handle, FragmentDescriptorPool, nullptr);
            FragmentDescriptorPool = VK_NULL_HANDLE;
        }
        if (FragmentDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(device_handle, FragmentDescriptorSetLayout, nullptr);
            FragmentDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (Pipeline)
        {
            vkDestroyPipeline(device_handle, Pipeline, nullptr);
            Pipeline = VK_NULL_HANDLE;
        }
        if (PipelineLayout)
        {
            vkDestroyPipelineLayout(device_handle, PipelineLayout, nullptr);
            PipelineLayout = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < FramebufferCollection.size(); ++i)
        {
            FramebufferCollection[i].Dispose();
        }
    }

    void StandardGraphicPipeline::CreateOrResizeFramebuffer(uint32_t width, uint32_t height)
    {
        if ((FramebufferSpecification.Width != width) || (FramebufferSpecification.Height != height))
        {
            FramebufferSpecification.Width  = width;
            FramebufferSpecification.Height = height;

            for (uint32_t i = 0; i < FramebufferCollection.size(); ++i)
            {
                if (FramebufferCollection[i].Framebuffer)
                {
                    FramebufferCollection[i].Dispose();
                }
                FramebufferCollection[i] = Rendering::Buffers::FramebufferVNext{FramebufferSpecification};
            }
        }
    }
    void StandardGraphicPipeline::UpdateDescriptorSet()
    {
        auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        for (size_t i = 0; i < FrameCount; ++i)
        {
            std::array<VkDescriptorBufferInfo, 2> buffer_infos = {};
            buffer_infos[0].buffer                             = UBOCameraPropertiesCollection[i].GetNativeBufferHandle();
            buffer_infos[0].range                              = VK_WHOLE_SIZE;
            buffer_infos[0].offset                             = 0;
            buffer_infos[1].buffer                             = UBOModelPropertiesCollection[i].GetNativeBufferHandle();
            buffer_infos[1].range                              = VK_WHOLE_SIZE;
            buffer_infos[1].offset                             = 0;

            VkWriteDescriptorSet write_descriptor_set = {};
            write_descriptor_set.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_descriptor_set.dstSet               = DescriptorSetCollection[i];
            write_descriptor_set.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write_descriptor_set.dstBinding           = 0;
            write_descriptor_set.dstArrayElement      = 0;
            write_descriptor_set.descriptorCount      = 2;
            write_descriptor_set.pBufferInfo          = buffer_infos.data();
            write_descriptor_set.pImageInfo           = nullptr; // Optional
            write_descriptor_set.pTexelBufferView     = nullptr; // Optional
            write_descriptor_set.pNext                = nullptr;

            VkDescriptorImageInfo descriptor_image_info = {};
            descriptor_image_info.sampler               = FramebufferCollection[i].Sampler;
            descriptor_image_info.imageView             = FramebufferCollection[i].AttachmentCollection[0]; // #0 is the color attachment
            descriptor_image_info.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write_descriptor_set_2 = {};
            write_descriptor_set_2.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_descriptor_set_2.dstSet               = FragmentDescriptorSetCollection[i];
            write_descriptor_set_2.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write_descriptor_set_2.dstBinding           = 0;
            write_descriptor_set_2.dstArrayElement      = 0;
            write_descriptor_set_2.descriptorCount      = 1;
            write_descriptor_set_2.pImageInfo           = &(descriptor_image_info);

            auto writers = std::array{write_descriptor_set, write_descriptor_set_2};
            vkUpdateDescriptorSets(performant_device.GetNativeDeviceHandle(), writers.size(), writers.data(), 0, nullptr);
        }
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
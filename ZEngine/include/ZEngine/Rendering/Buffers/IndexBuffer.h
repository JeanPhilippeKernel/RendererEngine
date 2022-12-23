#pragma once
#include <ZEngineDef.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Helpers/BufferHelper.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class IndexBuffer : public GraphicBuffer<T>
    {
    public:
        IndexBuffer() : GraphicBuffer<T>() {}

        void SetData(const std::vector<T>& content) override
        {
            GraphicBuffer<T>::SetData(content);

            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            VkBuffer       staging_buffer;
            VkDeviceMemory staging_buffer_memory;
            Helpers::CreateBuffer(performant_device, staging_buffer, staging_buffer_memory, static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                memory_properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(vkMapMemory(device_handle, staging_buffer_memory, 0, this->m_byte_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, content.data(), this->m_byte_size);
            vkUnmapMemory(device_handle, staging_buffer_memory);

            Helpers::CreateBuffer(performant_device, m_index_buffer, m_index_buffer_device_memory, static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memory_properties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            Helpers::CopyBuffer(performant_device, staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));

            vkDestroyBuffer(device_handle, staging_buffer, nullptr);
            vkFreeMemory(device_handle, staging_buffer_memory, nullptr);
        }

        ~IndexBuffer()
        {
            auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();

            vkDestroyBuffer(device_handle, m_index_buffer, nullptr);
            vkFreeMemory(device_handle, m_index_buffer_device_memory, nullptr);
        }

        void Bind() const override {}

        void Unbind() const override {}

    private:
        VkBuffer       m_index_buffer{VK_NULL_HANDLE};
        VkDeviceMemory m_index_buffer_device_memory{VK_NULL_HANDLE};
    };
} // namespace ZEngine::Rendering::Buffers

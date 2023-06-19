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

        void SetData(const T& content) override
        {
            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            if (this->m_byte_size < sizeof(T))
            {
                CleanUpMemory();
                GraphicBuffer<T>::SetData(content);
                Helpers::CreateBuffer(
                    performant_device,
                    m_index_buffer,
                    m_index_buffer_device_memory,
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    memory_properties,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }

            VkBuffer       staging_buffer;
            VkDeviceMemory staging_buffer_memory;
            Helpers::CreateBuffer(
                performant_device,
                staging_buffer,
                staging_buffer_memory,
                static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                memory_properties,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(vkMapMemory(device_handle, staging_buffer_memory, 0, this->m_byte_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, &content, this->m_byte_size);
            vkUnmapMemory(device_handle, staging_buffer_memory);

            Helpers::CopyBuffer(performant_device, staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));

            vkDestroyBuffer(device_handle, staging_buffer, nullptr);
            vkFreeMemory(device_handle, staging_buffer_memory, nullptr);
        }

        ~IndexBuffer()
        {
            Dispose();
        }

        VkBuffer GetNativeBufferHandle() const
        {
            return m_index_buffer;
        }

        void Dispose()
        {
            if (!m_disposed)
            {
                CleanUpMemory();
                m_disposed = true;
            }
        }

        void Bind() const override {}

        void Unbind() const override {}

    private:
        void CleanUpMemory()
        {
            if ((m_index_buffer != VK_NULL_HANDLE) && (m_index_buffer_device_memory != VK_NULL_HANDLE))
            {
                auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();

                vkDestroyBuffer(device_handle, m_index_buffer, nullptr);
                vkFreeMemory(device_handle, m_index_buffer_device_memory, nullptr);

                m_index_buffer               = VK_NULL_HANDLE;
                m_index_buffer_device_memory = VK_NULL_HANDLE;
            }
        }

    private:
        VkBuffer       m_index_buffer{VK_NULL_HANDLE};
        VkDeviceMemory m_index_buffer_device_memory{VK_NULL_HANDLE};
        bool           m_disposed{false};
    };

    template <>
    inline void IndexBuffer<std::vector<float>>::SetData(const std::vector<float>& content)
    {
        Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        auto                     device_handle     = performant_device.GetNativeDeviceHandle();
        const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

        if (this->m_byte_size < (content.size() * sizeof(float)))
        {
            CleanUpMemory();
            GraphicBuffer<std::vector<float>>::SetData(content);
            Helpers::CreateBuffer(
                performant_device,
                m_index_buffer,
                m_index_buffer_device_memory,
                static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                memory_properties,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        VkBuffer       staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        Helpers::CreateBuffer(
            performant_device,
            staging_buffer,
            staging_buffer_memory,
            static_cast<VkDeviceSize>(this->m_byte_size),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            memory_properties,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* memory_region;
        ZENGINE_VALIDATE_ASSERT(vkMapMemory(device_handle, staging_buffer_memory, 0, this->m_byte_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
        std::memcpy(memory_region, content.data(), this->m_byte_size);
        vkUnmapMemory(device_handle, staging_buffer_memory);

        Helpers::CopyBuffer(performant_device, staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));

        vkDestroyBuffer(device_handle, staging_buffer, nullptr);
        vkFreeMemory(device_handle, staging_buffer_memory, nullptr);
    }
} // namespace ZEngine::Rendering::Buffers

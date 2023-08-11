#pragma once
#include <ZEngineDef.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    class IndexBuffer : public IGraphicBuffer
    {
    public:
        IndexBuffer() : IGraphicBuffer() {}

        void SetData(const void* data, size_t byte_size)
        {
            if (!data)
            {
                ZENGINE_CORE_WARN("data is null")
                return;
            }

            if (byte_size == 0)
            {
                ZENGINE_CORE_WARN("data byte size is null")
                return;
            }

            if (this->m_byte_size < byte_size)
            {
                CleanUpMemory();
                this->m_byte_size = byte_size;
                m_index_buffer    = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }

            Hardwares::BufferView staging_buffer = Hardwares::VulkanDevice::CreateBuffer(
                static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            auto  device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(vkMapMemory(device, staging_buffer.Memory, 0, this->m_byte_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, data, this->m_byte_size);
            vkUnmapMemory(device, staging_buffer.Memory);

            Hardwares::VulkanDevice::CopyBuffer(staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));

            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, staging_buffer.Handle);
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, staging_buffer.Memory);
            staging_buffer = {};
        }

        template <typename T>
        inline void SetData(const std::vector<T>& content)
        {
            size_t byte_size = sizeof(T) * content.size();
            this->SetData(content.data(), byte_size);
        }

        ~IndexBuffer()
        {
            CleanUpMemory();
        }

        VkBuffer GetNativeBufferHandle() const
        {
            return m_index_buffer.Handle;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            if (m_index_buffer)
            {
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, m_index_buffer.Handle);
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, m_index_buffer.Memory);
                m_index_buffer = {};
            }
        }

    private:
        Hardwares::BufferView m_index_buffer;
    };
} // namespace ZEngine::Rendering::Buffers

#pragma once
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Hardwares/VulkanDevice.h>
#include <span>

namespace ZEngine::Rendering::Buffers
{

    class StorageBuffer : public IGraphicBuffer
    {
    public:
        explicit StorageBuffer() : IGraphicBuffer() {}

        void SetData(const void* data, uint32_t offset, size_t byte_size)
        {
            if (byte_size == 0)
            {
                return;
            }

            if (this->m_byte_size < byte_size)
            {
                /*
                 * Tracking the size change..
                 */
                m_last_byte_size = m_byte_size;

                CleanUpMemory();
                this->m_byte_size = byte_size;
                m_staging_buffer  = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                m_storage_buffer = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }

            if (!data)
            {
                return;
            }
            auto  device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(
                vkMapMemory(device, m_staging_buffer.Memory, static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(this->m_byte_size), 0, &memory_region) == VK_SUCCESS,
                "Failed to map the memory")
            std::memcpy(memory_region, data, this->m_byte_size);
            vkUnmapMemory(device, m_staging_buffer.Memory);

            Hardwares::VulkanDevice::CopyBuffer(m_staging_buffer, m_storage_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
        }

        template <typename T>
        inline void SetData(const std::vector<T>& content)
        {
            size_t byte_size = sizeof(T) * content.size();
            this->SetData(content.data(), 0, byte_size);
        }

        ~StorageBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const
        {
            return reinterpret_cast<void*>(m_storage_buffer.Handle);
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_storage_buffer.Handle, .offset = 0, .range = VK_WHOLE_SIZE};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            if (m_staging_buffer)
            {
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, m_staging_buffer.Handle);
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, m_staging_buffer.Memory);
                m_staging_buffer = {};
            }

            if (m_storage_buffer)
            {
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, m_storage_buffer.Handle);
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, m_storage_buffer.Memory);
                m_storage_buffer = {};
            }
        }

    private:
        Hardwares::BufferView  m_storage_buffer;
        Hardwares::BufferView  m_staging_buffer;
        VkDescriptorBufferInfo m_buffer_info;
    };

    struct StorageBufferSet : public Helpers::RefCounted
    {
        StorageBufferSet(uint32_t count = 0) : m_buffer_set(count) {}

        StorageBuffer& operator[](uint32_t index)
        {
            assert(index < m_buffer_set.size());
            return m_buffer_set[index];
        }

        const std::vector<StorageBuffer>& Data() const
        {
            return m_buffer_set;
        }

        std::vector<StorageBuffer>& Data()
        {
            return m_buffer_set;
        }

        void Dispose()
        {
            for (auto& buffer : m_buffer_set)
            {
                buffer.Dispose();
            }
        }

    private:
        std::vector<StorageBuffer> m_buffer_set;
    };
} // namespace ZEngine::Rendering::Buffers

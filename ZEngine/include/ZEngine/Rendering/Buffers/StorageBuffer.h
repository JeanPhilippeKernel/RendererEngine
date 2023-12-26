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
                m_storage_buffer  = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            }

            auto                  allocator = Hardwares::VulkanDevice::GetVmaAllocator();
            VkMemoryPropertyFlags mem_prop_flags;
            vmaGetAllocationMemoryProperties(allocator, m_storage_buffer.Allocation, &mem_prop_flags);

            if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(allocator, m_storage_buffer.Allocation, &allocation_info);
                if (data && allocation_info.pMappedData)
                {
                    ZENGINE_VALIDATE_ASSERT(
                        Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS,
                        "Failed to perform memory copy operation")
                }
            }
            else
            {
                Hardwares::BufferView staging_buffer = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(allocator, staging_buffer.Allocation, &allocation_info);

                if (data && allocation_info.pMappedData)
                {
                    ZENGINE_VALIDATE_ASSERT(
                        Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS,
                        "Failed to perform memory copy operation")
                    ZENGINE_VALIDATE_ASSERT(
                        vmaFlushAllocation(allocator, staging_buffer.Allocation, 0, static_cast<VkDeviceSize>(this->m_byte_size)) == VK_SUCCESS, "Failed to flush allocation")
                    Hardwares::VulkanDevice::CopyBuffer(staging_buffer, m_storage_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
                }

                /* Cleanup resource */
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(staging_buffer);
            }
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
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_storage_buffer.Handle, .offset = 0, .range = this->m_byte_size};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            if (m_storage_buffer)
            {
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(m_storage_buffer);
                m_storage_buffer = {};
            }
        }

    private:
        Hardwares::BufferView  m_storage_buffer;
        VkDescriptorBufferInfo m_buffer_info{};
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

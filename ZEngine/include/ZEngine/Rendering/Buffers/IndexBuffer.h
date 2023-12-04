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
                m_index_buffer    = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            }

            auto                  allocator = Hardwares::VulkanDevice::GetVmaAllocator();
            VkMemoryPropertyFlags mem_prop_flags;
            vmaGetAllocationMemoryProperties(allocator, m_index_buffer.Allocation, &mem_prop_flags);

            if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(allocator, m_index_buffer.Allocation, &allocation_info);
                if (data && allocation_info.pMappedData)
                {
                    std::memcpy(allocation_info.pMappedData, data, this->m_byte_size);
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
                    std::memcpy(allocation_info.pMappedData, data, this->m_byte_size);
                    ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(allocator, staging_buffer.Allocation, 0, VK_WHOLE_SIZE) == VK_SUCCESS, "Failed to flush allocation")
                    Hardwares::VulkanDevice::CopyBuffer(staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
                }

                /* Cleanup resource */
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(staging_buffer);
            }
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

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_index_buffer.Handle);
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
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(m_index_buffer);
                m_index_buffer = {};
            }
        }

    private:
        Hardwares::BufferView m_index_buffer;
    };
} // namespace ZEngine::Rendering::Buffers

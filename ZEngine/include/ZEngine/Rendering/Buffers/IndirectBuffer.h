#pragma once
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Buffers
{

    class IndirectBuffer : public IGraphicBuffer
    {
    public:
        explicit IndirectBuffer() : IGraphicBuffer() {}

        void SetData(const VkDrawIndirectCommand* data, size_t byte_size)
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
                m_command_count   = byte_size / sizeof(VkDrawIndirectCommand);
                m_indirect_buffer = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            }

            auto                  allocator = Hardwares::VulkanDevice::GetVmaAllocator();
            VkMemoryPropertyFlags mem_prop_flags;
            vmaGetAllocationMemoryProperties(allocator, m_indirect_buffer.Allocation, &mem_prop_flags);

            if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(allocator, m_indirect_buffer.Allocation, &allocation_info);
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
                    ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(allocator, staging_buffer.Allocation, 0, VK_WHOLE_SIZE) == VK_SUCCESS, "Failed to flush allocation")
                    Hardwares::VulkanDevice::CopyBuffer(staging_buffer, m_indirect_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
                }

                /* Cleanup resource */
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(staging_buffer);
            }
        }

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), content.size_bytes());
        }

        uint32_t GetCommandCount() const
        {
            return m_command_count;
        }

        ~IndirectBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_indirect_buffer.Handle);
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            m_command_count = 0;

            if (m_indirect_buffer)
            {
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(m_indirect_buffer);
                m_indirect_buffer = {};
            }
        }

    private:
        uint32_t              m_command_count{0};
        Hardwares::BufferView m_indirect_buffer;
    };



    struct IndirectBufferSet : public Helpers::RefCounted
    {
        IndirectBufferSet(uint32_t count = 0) : m_buffer_set(count) {}

        IndirectBuffer& operator[](uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_buffer_set.size(), "Index out of range")
            return m_buffer_set[index];
        }

        IndirectBuffer& At(uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_buffer_set.size(), "Index out of range")
            return m_buffer_set[index];
        }

        void SetData(uint32_t index, std::span<const VkDrawIndirectCommand> data)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_buffer_set.size(), "Index out of range")

            m_buffer_set[index].SetData(data);
        }

        const std::vector<IndirectBuffer>& Data() const
        {
            return m_buffer_set;
        }

        std::vector<IndirectBuffer>& Data()
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
        std::vector<IndirectBuffer> m_buffer_set;
    };
} // namespace ZEngine::Rendering::Buffers

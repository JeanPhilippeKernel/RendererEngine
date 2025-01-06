#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Helpers/HandleManager.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    class VertexBuffer : public IGraphicBuffer
    {
    public:
        explicit VertexBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        void SetData(const void* data, size_t byte_size)
        {
            if (byte_size == 0)
            {
                return;
            }

            if (this->m_byte_size != byte_size)
            {
                /*
                 * Tracking the size change..
                 */
                m_last_byte_size = m_byte_size;

                CleanUpMemory();
                this->m_byte_size = byte_size;
                m_vertex_buffer   = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            }

            VkMemoryPropertyFlags mem_prop_flags;
            vmaGetAllocationMemoryProperties(m_device->VmaAllocator, m_vertex_buffer.Allocation, &mem_prop_flags);

            if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(m_device->VmaAllocator, m_vertex_buffer.Allocation, &allocation_info);
                if (data && allocation_info.pMappedData)
                {
                    ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                }
            }
            else
            {
                BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

                VmaAllocationInfo allocation_info = {};
                vmaGetAllocationInfo(m_device->VmaAllocator, staging_buffer.Allocation, &allocation_info);

                if (data && allocation_info.pMappedData)
                {
                    ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                    ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocator, staging_buffer.Allocation, 0, static_cast<VkDeviceSize>(this->m_byte_size)) == VK_SUCCESS, "Failed to flush allocation")
                    m_device->CopyBuffer(staging_buffer, m_vertex_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
                }

                /* Cleanup resource */
                m_device->EnqueueBufferForDeletion(staging_buffer);
            }
        }

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), content.size_bytes());
        }

        ~VertexBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_vertex_buffer.Handle);
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_vertex_buffer.Handle, .offset = 0, .range = this->m_byte_size};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            if (m_vertex_buffer)
            {
                m_device->EnqueueBufferForDeletion(m_vertex_buffer);
                m_vertex_buffer = {};
            }
        }

    private:
        BufferView             m_vertex_buffer;
        VkDescriptorBufferInfo m_buffer_info{};
    };

    using VertexBufferSet       = IBufferSet<VertexBuffer>;
    using VertexBufferSetRef    = Helpers::Ref<VertexBufferSet>;
    using VertexBufferSetHandle = Helpers::Handle<VertexBufferSetRef>;

    template <>
    inline void VertexBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
        }
    }
} // namespace ZEngine::Rendering::Buffers

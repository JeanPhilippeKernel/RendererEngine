#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    class UniformBuffer : public IGraphicBuffer
    {
    public:
        explicit UniformBuffer() : IGraphicBuffer() {}

        explicit UniformBuffer(const UniformBuffer& rhs) = delete;

        explicit UniformBuffer(UniformBuffer& rhs)
        {
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};
        }

        explicit UniformBuffer(UniformBuffer&& rhs) noexcept
        {
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};
        }

        UniformBuffer& operator=(const UniformBuffer& rhs) = delete;

        UniformBuffer& operator=(UniformBuffer& rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};

            return *this;
        }

        UniformBuffer& operator=(UniformBuffer&& rhs) noexcept
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};

            return *this;
        }

        void SetData(const void* data, size_t byte_size)
        {
            if (byte_size == 0)
            {
                return;
            }

            if (this->m_byte_size < byte_size || (!m_uniform_buffer_mapped))
            {
                /*
                 * Tracking the size change..
                 */
                m_last_byte_size = m_byte_size;

                CleanUpMemory();
                this->m_byte_size = byte_size;
                m_uniform_buffer  = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
                m_uniform_buffer_mapped = true;
            }

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(Hardwares::VulkanDevice::GetVmaAllocator(), m_uniform_buffer.Allocation, &allocation_info);

            if (allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(
                    Helpers::secure_memset(allocation_info.pMappedData, 0, this->m_byte_size, allocation_info.size) == Helpers::MEMORY_OP_SUCCESS,
                    "Failed to perform memory set operation")
            }

            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(
                    Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS,
                    "Failed to perform memory copy operation")
            }
        }

        template <typename T>
        inline void SetData(const std::vector<T>& content)
        {
            size_t byte_size = sizeof(T) * content.size();
            this->SetData(content.data(), byte_size);
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_uniform_buffer.Handle);
        }

        void Dispose()
        {
            CleanUpMemory();
        }

        ~UniformBuffer()
        {
            CleanUpMemory();
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_uniform_buffer.Handle, .offset = 0, .range = m_byte_size};
            return m_buffer_info;
        }

    private:
        void CleanUpMemory()
        {
            if (m_uniform_buffer)
            {
                Hardwares::VulkanDevice::EnqueueBufferForDeletion(m_uniform_buffer);

                m_uniform_buffer_mapped = false;
                m_uniform_buffer        = {};
            }
        }

    private:
        bool                   m_uniform_buffer_mapped{false};
        Hardwares::BufferView  m_uniform_buffer{};
        VkDescriptorBufferInfo m_buffer_info{};
    };

    struct UniformBufferSet : public Helpers::RefCounted
    {
        UniformBufferSet(uint32_t count = 0) : m_buffer_set(count) {}

        UniformBuffer& operator[](uint32_t index)
        {
            assert(index < m_buffer_set.size());
            return m_buffer_set[index];
        }

        const std::vector<UniformBuffer>& Data() const
        {
            return m_buffer_set;
        }

        std::vector<UniformBuffer>& Data()
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
        std::vector<UniformBuffer> m_buffer_set;
    };

} // namespace ZEngine::Rendering::Buffers

#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    class UniformBuffer : public IGraphicBuffer
    {
    public:
        explicit UniformBuffer() : IGraphicBuffer()
        {
        }

        explicit UniformBuffer(const UniformBuffer& rhs) = delete;

        explicit UniformBuffer(UniformBuffer& rhs)
        {
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size                    = 0;
            rhs.m_uniform_buffer_mapped        = nullptr;
            rhs.m_uniform_buffer               = {};
        }

        explicit UniformBuffer(UniformBuffer&& rhs) noexcept
        {
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size                    = 0;
            rhs.m_uniform_buffer_mapped        = nullptr;
            rhs.m_uniform_buffer               = {};
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

            rhs.m_byte_size                    = 0;
            rhs.m_uniform_buffer_mapped        = nullptr;
            rhs.m_uniform_buffer               = {};

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

            rhs.m_byte_size                    = 0;
            rhs.m_uniform_buffer_mapped        = nullptr;
            rhs.m_uniform_buffer               = {};

            return *this;
        }

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

            if (this->m_byte_size < byte_size || (!m_uniform_buffer_mapped))
            {
                CleanUpMemory();
                this->m_byte_size = byte_size;
                m_uniform_buffer  = Hardwares::VulkanDevice::CreateBuffer(
                    static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
                ZENGINE_VALIDATE_ASSERT(vkMapMemory(device, m_uniform_buffer.Memory, 0, this->m_byte_size, 0, &m_uniform_buffer_mapped) == VK_SUCCESS, "Failed to map the memory")
            }
            std::memset(m_uniform_buffer_mapped, 0, this->m_byte_size);
            std::memcpy(m_uniform_buffer_mapped, data, this->m_byte_size);
        }

        template <typename T>
        inline void SetData(const std::vector<T>& content)
        {
            size_t byte_size = sizeof(T) * content.size();
            this->SetData(content.data(), byte_size);
        }

        VkBuffer GetNativeBufferHandle() const
        {
            return m_uniform_buffer.Handle;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

        ~UniformBuffer()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory()
        {
            if (m_uniform_buffer)
            {
                auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
                vkUnmapMemory(device, m_uniform_buffer.Memory);
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, m_uniform_buffer.Handle);
                Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, m_uniform_buffer.Memory);

                m_uniform_buffer_mapped = nullptr;
                m_uniform_buffer        = {};
            }
        }

    private:
        void*          m_uniform_buffer_mapped{nullptr};
        Hardwares::BufferView m_uniform_buffer;
    };
} // namespace ZEngine::Rendering::Buffers

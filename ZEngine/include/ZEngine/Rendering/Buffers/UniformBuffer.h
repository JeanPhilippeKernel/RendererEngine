#pragma once
#include <Engine.h>
#include <Helpers/BufferHelper.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class UniformBuffer : public GraphicBuffer<T>
    {
    public:
        explicit UniformBuffer() : GraphicBuffer<T>()
        {
            CleanUpMemory();
            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            Helpers::CreateBuffer(
                performant_device,
                m_uniform_buffer,
                m_uniform_buffer_device_memory,
                static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                memory_properties,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            ZENGINE_VALIDATE_ASSERT(
                vkMapMemory(device_handle, m_uniform_buffer_device_memory, 0, this->m_byte_size, 0, &m_uniform_buffer_mapped) == VK_SUCCESS, "Failed to map the memory")
            std::memset(m_uniform_buffer_mapped, 0, this->m_byte_size);
        }

        void SetData(const T& content) override
        {
            GraphicBuffer<T>::SetData(content);
            std::memset(m_uniform_buffer_mapped, 0, this->m_byte_size);
            std::memcpy(m_uniform_buffer_mapped, &content, this->m_byte_size);
        }

        void SetData(const void* data, size_t byte_size)
        {
            this->m_byte_size = byte_size;
            this->m_data      = *(reinterpret_cast<T*>(data));

            std::memset(m_uniform_buffer_mapped, 0, this->m_byte_size);
            std::memcpy(m_uniform_buffer_mapped, data, this->m_byte_size);
        }

        VkBuffer GetNativeBufferHandle() const
        {
            return m_uniform_buffer;
        }

        ~UniformBuffer()
        {
            CleanUpMemory();
        }

        void Bind() const override {}

        void Unbind() const override {}

    private:
        void CleanUpMemory()
        {
            if ((m_uniform_buffer != VK_NULL_HANDLE) && (m_uniform_buffer_device_memory != VK_NULL_HANDLE))
            {
                auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();
                vkDestroyBuffer(device_handle, m_uniform_buffer, nullptr);
                vkFreeMemory(device_handle, m_uniform_buffer_device_memory, nullptr);

                m_uniform_buffer_mapped        = nullptr;
                m_uniform_buffer               = VK_NULL_HANDLE;
                m_uniform_buffer_device_memory = VK_NULL_HANDLE;
            }
        }

    private:
        void*          m_uniform_buffer_mapped{nullptr};
        VkBuffer       m_uniform_buffer{VK_NULL_HANDLE};
        VkDeviceMemory m_uniform_buffer_device_memory{VK_NULL_HANDLE};
    };

    template <>
    inline void UniformBuffer<std::vector<float>>::SetData(const std::vector<float>& content)
    {
        GraphicBuffer<std::vector<float>>::SetData(content);
        std::memset(m_uniform_buffer_mapped, 0, this->m_byte_size);
        std::memcpy(m_uniform_buffer_mapped, content.data(), this->m_byte_size);
    }
} // namespace ZEngine::Rendering::Buffers

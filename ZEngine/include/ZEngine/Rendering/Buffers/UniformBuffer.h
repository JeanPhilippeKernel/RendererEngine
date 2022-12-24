#pragma once
#include <ZEngine.h>
#include <Helpers/BufferHelper.h>
#include <Rendering/Buffers/GraphicBuffer.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class UniformBuffer : public GraphicBuffer<T>
    {
    public:
        UniformBuffer(unsigned int binding_index = 0) : GraphicBuffer<T>()
        {
            //m_binding_index = binding_index;
        }

        void SetData(const std::vector<T>& content) override
        {
            CleanUpMemory();

            GraphicBuffer<T>::SetData(content);

            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            Helpers::CreateBuffer(performant_device, m_uniform_buffer, m_uniform_buffer_device_memory, static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, memory_properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            ZENGINE_VALIDATE_ASSERT(
                vkMapMemory(device_handle, m_uniform_buffer_device_memory, 0, this->m_byte_size, 0, &m_uniform_buffer_mapped) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(m_uniform_buffer_mapped, content.data(), this->m_byte_size);
        }


        void SetData(const void* data, size_t byte_size)
        {
            CleanUpMemory();

            this->m_data_size = 1;
            this->m_byte_size = byte_size;

            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            Helpers::CreateBuffer(performant_device, m_uniform_buffer, m_uniform_buffer_device_memory, static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, memory_properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            ZENGINE_VALIDATE_ASSERT(
                vkMapMemory(device_handle, m_uniform_buffer_device_memory, 0, this->m_byte_size, 0, &m_uniform_buffer_mapped) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(m_uniform_buffer_mapped, data, this->m_byte_size);
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
            if (m_uniform_buffer && m_uniform_buffer_device_memory)
            {
                auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();
                vkDestroyBuffer(device_handle, m_uniform_buffer, nullptr);
                vkFreeMemory(device_handle, m_uniform_buffer_device_memory, nullptr);
            }
        }

    private:
        void*          m_uniform_buffer_mapped{nullptr};
        VkBuffer       m_uniform_buffer{VK_NULL_HANDLE};
        VkDeviceMemory m_uniform_buffer_device_memory{VK_NULL_HANDLE};
    };
} // namespace ZEngine::Rendering::Buffers

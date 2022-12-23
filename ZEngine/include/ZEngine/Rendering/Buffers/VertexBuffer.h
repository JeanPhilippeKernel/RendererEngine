#pragma once
#include <Engine.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/BufferLayout.h>
#include <Rendering/Renderers/Storages/IVertex.h>
#include <Helpers/BufferHelper.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class VertexBuffer : public GraphicBuffer<T>
    {
    public:
        explicit VertexBuffer(unsigned int vertex_count) : GraphicBuffer<T>(), m_vertex_count(vertex_count)
        {
            // ToDo : Should be moved to graphic pipeline
            m_binding_description.binding   = 0;
            m_binding_description.stride    = sizeof(Renderers::Storages::IVertex);
            m_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        }

        unsigned int GetVertexCount() const
        {
            return m_vertex_count;
        }

        void SetData(const std::vector<T>& content) override
        {
            GraphicBuffer<T>::SetData(content);

            Hardwares::VulkanDevice& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto                     device_handle     = performant_device.GetNativeDeviceHandle();
            const auto&              memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            VkBuffer       staging_buffer;
            VkDeviceMemory staging_buffer_memory;
            Helpers::CreateBuffer(performant_device, staging_buffer, staging_buffer_memory, static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                memory_properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(vkMapMemory(device_handle, staging_buffer_memory, 0, this->m_byte_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, content.data(), this->m_byte_size);
            vkUnmapMemory(device_handle, staging_buffer_memory);

            Helpers::CreateBuffer(performant_device, m_vertex_buffer, m_vertex_buffer_device_memory, static_cast<VkDeviceSize>(this->m_byte_size),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, memory_properties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            Helpers::CopyBuffer(performant_device, staging_buffer, m_vertex_buffer, static_cast<VkDeviceSize>(this->m_byte_size));

            vkDestroyBuffer(device_handle, staging_buffer, nullptr);
            vkFreeMemory(device_handle, staging_buffer_memory, nullptr);
        }

        ~VertexBuffer()
        {
            auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();

            vkDestroyBuffer(device_handle, m_vertex_buffer, nullptr);
            vkFreeMemory(device_handle, m_vertex_buffer_device_memory, nullptr);
        }

        void Bind() const override
        {
        }

        void Unbind() const override
        {
        }

        void SetLayout(const Layout::BufferLayout<T>& layout)
        {
            m_layout = layout;
            UpdateLayout();
        }

        void SetLayout(Layout::BufferLayout<T>&& layout)
        {
            m_layout = std::move(layout);
            UpdateLayout();
        }

        const Layout::BufferLayout<T>& GetLayout() const
        {
            return m_layout;
        }

    protected:
        void UpdateLayout()
        {
            std::vector<Layout::ElementLayout<T>>& elements = m_layout.GetElementLayout();

            m_attribute_description_collection.resize(elements.size());

            for (auto i = 0; i < m_attribute_description_collection.size(); ++i)
            {
                m_attribute_description_collection[i].binding  = 0;
                m_attribute_description_collection[i].location = i;
                m_attribute_description_collection[i].format   = (VkFormat) elements[i].GetFormat();
                m_attribute_description_collection[i].offset   = elements[i].GetOffset();
            }
        }

    private:
        unsigned int                                   m_vertex_count{0}; // Todo : this property should be removed ?
        VkBuffer                                       m_vertex_buffer{VK_NULL_HANDLE};
        VkDeviceMemory                                 m_vertex_buffer_device_memory{VK_NULL_HANDLE};
        VkVertexInputBindingDescription                m_binding_description{};
        std::vector<VkVertexInputAttributeDescription> m_attribute_description_collection{};
        Layout::BufferLayout<T>                        m_layout;
    };
} // namespace ZEngine::Rendering::Buffers

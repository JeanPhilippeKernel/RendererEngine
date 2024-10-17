#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <array>

namespace ZEngine::Rendering::Renderers::Storages
{

    struct IVertex
    {
        glm::vec3 m_position{0.0f, 0.0f, 0.0f};
        glm::vec3 m_normal{0.0f, 0.0f, 0.0f};
        glm::vec2 m_texture_coord{0.0f, 0.0f};

        static const std::array<VkVertexInputBindingDescription, 1>& GetVertexInputBindingDescription()
        {
            m_binding_description[0].binding   = 0;
            m_binding_description[0].stride    = sizeof(IVertex);
            m_binding_description[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return m_binding_description;
        }

        static const std::array<VkVertexInputAttributeDescription, 3>& GetVertexAttributeDescription()
        {
            m_attribute_description[0]          = {};
            m_attribute_description[0].binding  = 0;
            m_attribute_description[0].location = 0;
            m_attribute_description[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
            m_attribute_description[0].offset   = offsetof(IVertex, m_position);

            m_attribute_description[1]          = {};
            m_attribute_description[1].binding  = 0;
            m_attribute_description[1].location = 1;
            m_attribute_description[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
            m_attribute_description[1].offset   = offsetof(IVertex, m_normal);

            m_attribute_description[2]          = {};
            m_attribute_description[2].binding  = 0;
            m_attribute_description[2].location = 2;
            m_attribute_description[2].format   = VK_FORMAT_R32G32_SFLOAT;
            m_attribute_description[2].offset   = offsetof(IVertex, m_texture_coord);

            return m_attribute_description;
        }

    private:
        inline static std::array<VkVertexInputBindingDescription, 1>   m_binding_description   = {};
        inline static std::array<VkVertexInputAttributeDescription, 3> m_attribute_description = {};
    };
} // namespace ZEngine::Rendering::Renderers::Storages

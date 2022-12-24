#include <pch.h>
#include <Rendering/Renderers/Storages/GraphicVertex.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Storages
{
    GraphicVertex::GraphicVertex() : IVertex(), m_buffer()
    {
        _UpdateBuffer();
    }

    GraphicVertex::GraphicVertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec2& texture_coord) : IVertex()
    {
        m_position      = position;
        m_normal        = normal;
        m_texture_coord = texture_coord;

        _UpdateBuffer();
    }

    glm::vec3 GraphicVertex::GetPosition() const
    {
        return m_position;
    }

    glm::vec3 GraphicVertex::GetNormal() const
    {
        return m_normal;
    }

    glm::vec2 GraphicVertex::GetTextureCoord() const
    {
        return m_texture_coord;
    }

    void GraphicVertex::SetPosition(const glm::vec3& value)
    {
        m_position  = value;
        m_buffer[0] = m_position.x;
        m_buffer[1] = m_position.y;
        m_buffer[2] = m_position.z;
    }

    void GraphicVertex::SetNormal(const glm::vec3& value)
    {
        m_normal    = value;
        m_buffer[3] = m_normal.x;
        m_buffer[4] = m_normal.y;
        m_buffer[5] = m_normal.z;
    }


    void GraphicVertex::SetTextureCoord(const glm::vec2& value)
    {
        m_texture_coord = value;
        m_buffer[6]     = m_texture_coord.x;
        m_buffer[7]     = m_texture_coord.y;
    }

    void GraphicVertex::TransformPosition(const glm::mat4& matrix)
    {
        glm::vec4 position = glm::vec4(m_position, 1.0f);
        position           = matrix * position;
        SetPosition({position.x, position.y, position.z});
    }

    void GraphicVertex::_UpdateBuffer()
    {
        m_buffer[0] = m_position.x;
        m_buffer[1] = m_position.y;
        m_buffer[2] = m_position.z;

        m_buffer[3] = m_normal.x;
        m_buffer[4] = m_normal.y;
        m_buffer[5] = m_normal.z;

        m_buffer[6] = m_texture_coord.x;
        m_buffer[7] = m_texture_coord.y;
    }

    Buffers::Layout::BufferLayout<float> GraphicVertex::Descriptor::m_internal_layout{{Buffers::Layout::ElementLayout<float>(3, "position", false, VK_FORMAT_R32G32B32_SFLOAT),
        Buffers::Layout::ElementLayout<float>(3, "normal", false, VK_FORMAT_R32G32B32_SFLOAT),
        Buffers::Layout::ElementLayout<float>(2, "texture_coord", false, VK_FORMAT_R32G32_SFLOAT)}};

} // namespace ZEngine::Rendering::Renderers::Storages

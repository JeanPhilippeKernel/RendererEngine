#pragma once
#include <array>
#include <glm/glm/glm.hpp>

#include <Rendering/Renderers/Storages/IVertex.h>
#include <Rendering/Buffers/BufferLayout.h>

namespace ZEngine::Rendering::Renderers::Storages {

    class GraphicVertex : public IVertex {
    public:
        struct Descriptor;

    public:
        explicit GraphicVertex();
        explicit GraphicVertex(const glm::vec3& position, const glm::vec2& texture_coord = {0.0f, 0.0f});

        ~GraphicVertex() = default;

        glm::vec3 GetPosition() const;
        glm::vec2 GetTextureCoord() const;

        void SetPosition(const glm::vec3& value);
        void SetTextureCoord(const glm::vec2& value);

        void ApplyMatrixToPosition(const glm::mat4& matrix);

        const std::array<float, 5>& GetData() const {
            return m_buffer;
        }

    private:
        void _UpdateBuffer();

    private:
        std::array<float, 5> m_buffer;
    };

    struct GraphicVertex::Descriptor {
    public:
        static Buffers::Layout::BufferLayout<float>& GetLayout() {
            return m_internal_layout;
        }

    private:
        static Buffers::Layout::BufferLayout<float> m_internal_layout;
    };
} // namespace ZEngine::Rendering::Renderers::Storages

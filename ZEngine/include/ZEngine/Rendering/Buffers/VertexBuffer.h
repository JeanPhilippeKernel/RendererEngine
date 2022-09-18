#pragma once

#include <ZEngineDef.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/BufferLayout.h>
#include <Rendering/Renderers/Storages/IVertex.h>
#include <Core/IGraphicObject.h>

namespace ZEngine::Rendering::Buffers {

    template <typename T>
    class VertexBuffer : public GraphicBuffer<T>, public Core::IGraphicObject {
    public:
        explicit VertexBuffer(unsigned int vertex_count) : GraphicBuffer<T>(), m_vertex_count(vertex_count) {
            unsigned int buffer_reserved_byte_size = m_vertex_count * sizeof(Renderers::Storages::IVertex);
#ifdef _WIN32
            glCreateBuffers(1, &m_vertex_buffer_id);
#else
            glGenBuffers(1, &m_vertex_buffer_id);
#endif
            glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
            glBufferData(GL_ARRAY_BUFFER, buffer_reserved_byte_size, nullptr, GL_DYNAMIC_DRAW);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        unsigned int GetVertexCount() const {
            return m_vertex_count;
        }

        void SetData(const std::vector<T>& data) override {
            GraphicBuffer<T>::SetData(data);
            glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
            glBufferSubData(GL_ARRAY_BUFFER, 0, this->m_byte_size, this->m_data.data());
        }

        ~VertexBuffer() {
            glDeleteBuffers(1, &m_vertex_buffer_id);
        }

        void Bind() const override {
            glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
        }

        void Unbind() const override {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        GLuint GetIdentifier() const override {
            return m_vertex_buffer_id;
        }

        void SetLayout(const Layout::BufferLayout<T>& layout) {
            m_layout = layout;
            UpdateLayout();
        }

        void SetLayout(Layout::BufferLayout<T>&& layout) {
            m_layout = std::move(layout);
            UpdateLayout();
        }

        const Layout::BufferLayout<T>& GetLayout() const {
            return m_layout;
        }

    protected:
        void UpdateLayout() {
            auto& elements = m_layout.GetElementLayout();

            auto start = std::next(std::begin(elements)); // We start at the second element since the offset of this first element should be zero
            auto end   = std::end(elements);

            while (start != end) {
                auto current = std::prev(start);
                auto value   = current->GetSize() + current->GetOffset();
                start->SetOffset(value);

                start = std::next(start);
            }
        }

    private:
        GLuint                  m_vertex_buffer_id{0};
        unsigned int            m_vertex_count{0};
        Layout::BufferLayout<T> m_layout;
    };
} // namespace ZEngine::Rendering::Buffers

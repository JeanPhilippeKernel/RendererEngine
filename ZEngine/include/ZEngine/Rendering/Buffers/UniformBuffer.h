#pragma once
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/BufferLayout.h>
#include <ZEngineDef.h>

#include <Core/IGraphicObject.h>

namespace ZEngine::Rendering::Buffers {

    template <typename T>
    class UniformBuffer : public GraphicBuffer<T>, public Core::IGraphicObject {

    public:
        UniformBuffer(GLuint binding_index = 0) : GraphicBuffer<T>() {
#ifdef _WIN32
            glCreateBuffers(1, &m_uniform_buffer_id);
#else
            glGenBuffers(1, &m_uniform_buffer_id);
#endif
            m_binding_index = binding_index;
        }

        void SetData(const std::vector<T>& data) override {
            GraphicBuffer<T>::SetData(data);
            glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffer_id);
            glBufferData(GL_UNIFORM_BUFFER, this->m_byte_size, nullptr, GL_DYNAMIC_DRAW);

            if constexpr (
                std::is_same_v<T,
                    Maths::
                        Matrix4> || std::is_same_v<T, Maths::Matrix2> || std::is_same_v<T, Maths::Matrix3> || std::is_same_v<T, Maths::Vector4> || std::is_same_v<T, Maths::Vector3> || std::is_same_v<T, Maths::Vector2> || std::is_same_v<T, Maths::Vector1>) {
                size_t byte_size = 0;
                for (auto& value : data) {
                    glBufferSubData(GL_UNIFORM_BUFFER, byte_size, sizeof(T), Maths::value_ptr(value));
                    byte_size += sizeof(T);
                }
            } else {
                glBufferSubData(GL_UNIFORM_BUFFER, 0, this->m_byte_size, this->m_data.data());
            }

            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            glBindBufferRange(GL_UNIFORM_BUFFER, m_binding_index, m_uniform_buffer_id, 0, this->m_byte_size);
        }


        void SetData(const void* data, size_t byte_size) {
            this->m_data_size = 1;
            this->m_byte_size = byte_size;
            glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffer_id);
            glBufferData(GL_UNIFORM_BUFFER, this->m_byte_size, nullptr, GL_DYNAMIC_DRAW);

            glBufferSubData(GL_UNIFORM_BUFFER, 0, this->m_byte_size, data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            glBindBufferRange(GL_UNIFORM_BUFFER, m_binding_index, m_uniform_buffer_id, 0, this->m_byte_size);
        }

        ~UniformBuffer() {
            glDeleteBuffers(1, &m_uniform_buffer_id);
        }

        void Bind() const override {
            glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffer_id);
        }

        void Unbind() const override {
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

        GLuint GetIdentifier() const override {
            return m_uniform_buffer_id;
        }

    private:
        GLuint m_uniform_buffer_id;
        GLuint m_binding_index;
    };
} // namespace ZEngine::Rendering::Buffers

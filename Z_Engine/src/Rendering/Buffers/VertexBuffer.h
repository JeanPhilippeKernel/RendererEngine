#pragma once
#include <GL/glew.h>

#include "GraphicBuffer.h"
#include "BufferLayout.h"

#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Buffers {

	template<typename T>
	class Z_ENGINE_API VertexBuffer : public GraphicBuffer<T> {
	public:
		explicit VertexBuffer(const std::vector<T>& data) 
			:GraphicBuffer<T>(data)
		{
			glCreateBuffers(1, &m_vertex_buffer_id);
			glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
			glBufferData(GL_ARRAY_BUFFER, this->m_byte_size, this->m_data.data(), GL_STATIC_DRAW);
		}

		explicit VertexBuffer(const std::vector<T>& data, Layout::BufferLayout<T>& layout)
			:GraphicBuffer<T>(data)
		{
			glCreateBuffers(1, &m_vertex_buffer_id);
			glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
			glBufferData(GL_ARRAY_BUFFER, this->m_byte_size, this->m_data.data(), GL_STATIC_DRAW);

			this->SetLayout(layout);
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

	private:
		GLuint m_vertex_buffer_id{ 0 };
		Layout::BufferLayout<T> m_layout;
	};
}

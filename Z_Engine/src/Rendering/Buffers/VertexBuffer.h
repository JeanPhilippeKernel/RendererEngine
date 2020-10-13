#pragma once
#include <GL/glew.h>

#include "GraphicBuffer.h"
#include "BufferLayout.h"

#include "../../Z_EngineDef.h"

#include "../Renderers/Storages/IVertex.h"

namespace Z_Engine::Rendering::Buffers {

	template<typename T>
	class VertexBuffer : public GraphicBuffer<T> {
	public:			

		explicit VertexBuffer(unsigned int max_vertices = 1000) : GraphicBuffer<T>(), m_max_vertices(max_vertices) {
			glCreateBuffers(1, &m_vertex_buffer_id);
			glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer_id);
			glBufferData(GL_ARRAY_BUFFER, sizeof(Renderers::Storages::IVertex) * m_max_vertices, nullptr, GL_DYNAMIC_DRAW);

			glBindBuffer(GL_ARRAY_BUFFER, 0);
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

	private:
		GLuint m_vertex_buffer_id{ 0 };
		Layout::BufferLayout<T> m_layout;
		
		size_t m_max_vertices{ 0 };
	};
}

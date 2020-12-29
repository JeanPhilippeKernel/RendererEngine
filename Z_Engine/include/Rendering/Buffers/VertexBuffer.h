#pragma once

#include "GraphicBuffer.h"
#include "BufferLayout.h"

#include "../../Z_EngineDef.h"

#include "../Renderers/Storages/IVertex.h"
#include "../../Core/IGraphicObject.h"


namespace Z_Engine::Rendering::Buffers {

	template<typename T>
	class VertexBuffer : public GraphicBuffer<T>, public Core::IGraphicObject {
	public:			

		explicit VertexBuffer(unsigned int vertex_count) : GraphicBuffer<T>(), m_vertex_count(vertex_count) {
			unsigned int buffer_reserved_byte_size = m_vertex_count * sizeof(Renderers::Storages::IVertex);
			
			glCreateBuffers(1, &m_vertex_buffer_id);
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

	private:
		GLuint			m_vertex_buffer_id{ 0 };
		unsigned int	m_vertex_count{ 0 };
	};
}

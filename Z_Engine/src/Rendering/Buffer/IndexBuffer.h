#pragma once
#include <GL/glew.h>
#include "GraphicBuffer.h"
#include "BufferLayout.h"
#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Buffer {

	template <typename T>
	class Z_ENGINE_API IndexBuffer : public GraphicBuffer<T> {
	public:

		IndexBuffer(const std::vector<T>& indices)
			: GraphicBuffer<T>(indices)
		{
			glCreateBuffers(1, &m_element_buffer_id);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_element_buffer_id);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->m_byte_size, this->m_data.data(), GL_STATIC_DRAW);
		}

		//IndexBuffer(const std::vector<T>& indices, Layout::BufferLayout<T> layout) 
		//	: GraphicBuffer<T>(indices)
		//{
		//	glCreateBuffers(1, &m_element_buffer_id);
		//	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_element_buffer_id);
		//	glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->m_byte_size, this->m_data.data(), GL_STATIC_DRAW);

		//	this->SetLayout(layout);
		//}

		~IndexBuffer() {
			glDeleteBuffers(1, &m_element_buffer_id);
		}

		void Bind() const override {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_element_buffer_id);
		}

		void Unbind() const override {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	
	private:
		GLuint m_element_buffer_id;
	};
}

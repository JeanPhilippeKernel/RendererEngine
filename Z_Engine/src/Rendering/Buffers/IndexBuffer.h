#pragma once
#include <GL/glew.h>
#include "GraphicBuffer.h"
#include "BufferLayout.h"
#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Buffers {

	template <typename T>
	class IndexBuffer : public GraphicBuffer<T> {
	public:

		IndexBuffer(unsigned int  indice_count = 0) :GraphicBuffer<T>()
		{
			//this->m_data = std::vector<T>(indice_count, 0.0f);

//			this->m_data.reserve(indice_count);
			glCreateBuffers(1, &m_element_buffer_id);
		}
		
		//IndexBuffer(const std::vector<T>& indices)
		//	: GraphicBuffer<T>(indices)
		//{
		//	glCreateBuffers(1, &m_element_buffer_id);
		//	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_element_buffer_id);
		//	glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->m_byte_size, this->m_data.data(), GL_DYNAMIC_DRAW);
		//}


		void SetData(const std::vector<T>& data) override {
			GraphicBuffer<T>::SetData(std::move(data));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_element_buffer_id);

			glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->m_byte_size, nullptr, GL_STATIC_DRAW);
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, this->m_byte_size, this->m_data.data());
		}


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

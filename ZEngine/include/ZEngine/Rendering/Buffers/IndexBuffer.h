#pragma once
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/BufferLayout.h>
#include <Z_EngineDef.h>

#include <Core/IGraphicObject.h>


namespace ZEngine::Rendering::Buffers {

	template <typename T>
	class IndexBuffer 
		: 
		public GraphicBuffer<T>, 
		public Core::IGraphicObject {

	public:

		IndexBuffer() : GraphicBuffer<T>()
		{
			glCreateBuffers(1, &m_element_buffer_id);
		}
		

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

		GLuint GetIdentifier() const override {
			return m_element_buffer_id;
		}
	
	private:
		GLuint m_element_buffer_id;
	};
}

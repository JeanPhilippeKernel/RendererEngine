#pragma once
#include <memory>
#include <vector>

#include "../../Core/Utility.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"

#include "../../Z_EngineDef.h"

#include "../../Core/IGraphicObject.h"


namespace Z_Engine::Rendering::Buffers {

	template <typename T, typename K>
	class VertexArray : public Core::IGraphicObject {

	public:
		VertexArray()
		{
			glCreateVertexArrays(1, &m_vertex_array_id);
		}

		~VertexArray() {
			glDeleteVertexArrays(1, &m_vertex_array_id);
		}

		void Bind() const {
			glBindVertexArray(m_vertex_array_id);
		}
		void Unbind() const {
			glBindVertexArray(0);   
		}

		void AddVertexBuffer(const Ref<VertexBuffer<T>>& vertex_buffer) {
			
			glBindVertexArray(m_vertex_array_id);

				const Layout::BufferLayout<T>& buffer_layout = vertex_buffer->GetLayout();
				const std::vector<Layout::ElementLayout<T>>& element_layouts = buffer_layout.GetElementLayout();
									  
				int x = 0;		   
				for (const auto& element : element_layouts)
				{
					glEnableVertexAttribArray(x);
					glVertexAttribPointer(
						x,
						element.GetCount(),
						Core::Utility::ToGraphicCardType(element.GetDataType()), //GL_FLOAT
						static_cast<int>(element.GetNormalized()), // element.GetNormalized() == false ? GL_FALSE : GL_TRUE,
						buffer_layout.GetStride(),
						reinterpret_cast<const void *>(element.GetOffset())
					);
					++x;
				}
			glBindVertexArray(0);

			m_vertices_buffer.push_back(vertex_buffer);
		}

		void SetIndexBuffer(const Ref<IndexBuffer<K>>& index_buffer) {
			m_index_buffer = index_buffer;
		
			glBindVertexArray(m_vertex_array_id);
				m_index_buffer->Bind();
			glBindVertexArray(0);

		}

		const Ref<IndexBuffer<K>>& GetIndexBuffer() const {
			return m_index_buffer;
		}

		GLuint GetIdentifier() const override {
			return m_vertex_array_id;
		}

	private:
		std::vector<Ref<VertexBuffer<T>>> m_vertices_buffer;
		Ref<IndexBuffer<K>> m_index_buffer;

		GLuint m_vertex_array_id{ 0 };
	};

}

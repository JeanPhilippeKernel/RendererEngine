#pragma once
#include "../Buffers/VertexArray.h"
#include "../Buffers/VertexBuffer.h"
#include "../Buffers/IndexBuffer.h"
#include "../Shaders/Shader.h"



// 

namespace Z_Engine::Rendering::Renderer {
	
	template <typename T, typename K>
	class GraphicRendererStorage {
	public:
		explicit GraphicRendererStorage() = default;
		explicit GraphicRendererStorage(
			Ref<Shaders::Shader>			& shader, 
			Ref<Buffers::VertexBuffer<T>>	& vertex_buffer, 
			Ref<Buffers::IndexBuffer<K>>	& index_buffer, 
			Ref<Buffers::VertexArray<T, K>>	& m_vertex_array)
			:
			m_shader(shader), 
			m_vertex_buffer(vertex_buffer),
			m_vertex_array(m_vertex_array),
			m_index_buffer(index_buffer)  	
		{
		}
				 
		explicit GraphicRendererStorage(
			Ref<Shaders::Shader>			& shader,
			Ref<Buffers::VertexBuffer<T>>	& vertex_buffer,
			Ref<Buffers::IndexBuffer<K>>	& index_buffer)
			:
			m_shader(shader), 
			m_vertex_buffer(vertex_buffer), 
			m_vertex_array(new Buffers::VertexArray<T, K>()),
			m_index_buffer(index_buffer)
		{
			m_vertex_array->SetIndexBuffer(index_buffer);
			m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		}

		explicit GraphicRendererStorage(
			Ref<Shaders::Shader>		& shader,
			const std::vector<T>		& vertex_data,
			const std::vector<K>		& index_data, 
			Z_Engine::Rendering::Buffers::Layout::BufferLayout<T>& layout)
			:
			m_shader(shader), 
			m_vertex_buffer(new Buffers::VertexBuffer<T>(vertex_data, layout)),
			m_vertex_array(new Buffers::VertexArray<T, K>()),
			m_index_buffer(new Buffers::IndexBuffer<K>(index_data))

		{
			m_vertex_array->SetIndexBuffer(m_index_buffer);
			m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		}

		~GraphicRendererStorage() = default;


		 constexpr Ref<Buffers::VertexArray<T, K>> GetVertexArray() { return m_vertex_array; }
		 constexpr Ref<Shaders::Shader> GetShader() { return m_shader; }



	protected:
		Ref<Shaders::Shader>			m_shader;
		Ref<Buffers::VertexBuffer<T>>	m_vertex_buffer;
		Ref<Buffers::VertexArray<T, K>> m_vertex_array;
		Ref<Buffers::IndexBuffer<K>>	m_index_buffer;
	};
}

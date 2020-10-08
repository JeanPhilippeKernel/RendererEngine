#pragma once
#include <vector>
#include <algorithm>

#include "GraphicVertex.h"
#include "../../Buffers/VertexArray.h"
#include "../../Buffers/VertexBuffer.h"
#include "../../Buffers/IndexBuffer.h"
#include "../../Shaders/Shader.h"


namespace Z_Engine::Rendering::Renderers::Storages {
	
	template <typename T, typename K>
	class GraphicRendererStorage {
	public:
		GraphicRendererStorage();
		~GraphicRendererStorage() = default;

		void SetShader(const Ref<Shaders::Shader>& shader) { 
			m_shader = shader; 
		}
		
		void SetVertexBufferLayout(std::initializer_list<Buffers::Layout::ElementLayout<T>>&& element_layout ) {
			Buffers::Layout::BufferLayout<T> buffer_layout{element_layout};
			m_vertex_buffer->SetLayout(std::move(buffer_layout));
		}

		void AddVertices(const std::vector<GraphicVertex>& vertices) {
			std::for_each(std::begin(vertices), std::end(vertices), [this](const GraphicVertex& vertex) {
					m_internal_vertices_buffer_cursor->m_position				= vertex.m_position;
					m_internal_vertices_buffer_cursor->m_color					= vertex.m_color;
					m_internal_vertices_buffer_cursor->m_texture_coord			= vertex.m_texture_coord;
					m_internal_vertices_buffer_cursor->m_texture_id				= vertex.m_texture_id;
					m_internal_vertices_buffer_cursor->m_texture_tint_color		= vertex.m_texture_tint_color;
					m_internal_vertices_buffer_cursor->m_texture_tiling_factor	= vertex.m_texture_tiling_factor;

					m_internal_vertices_buffer_cursor++;
				});

			// Todo :  Flush buffer and immediate draw if buffer is full...
		}
		
		void UpdateBuffers() {
			std::memcpy(&m_internal_raw_vertices[0], m_internal_vertices.data(), m_internal_vertices.size());
			m_vertex_buffer->SetData(m_internal_raw_vertices);
			m_index_buffer->SetData(m_internal_index);		

			m_vertex_array->SetIndexBuffer(m_index_buffer);
			m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		}

		void FlushBuffers() {
			m_internal_vertices_buffer_cursor = &m_internal_vertices[0]; // reset the position of the cursor
			std::memset(&m_internal_raw_vertices[0], 0, m_internal_raw_vertices.size());
		}


		Ref<Buffers::VertexArray<T, K>>& GetVertexArray() { return m_vertex_array; }
		Ref<Shaders::Shader>& GetShader() { return m_shader; }


	private:
		unsigned int					M_MAX_QUAD;
		unsigned int					M_MAX_VERTICES_PER_QUAD;
		unsigned int					M_MAX_INDEX_PER_QUAD;
		unsigned int					M_MAX_INDICES;
		
		std::vector<IVertex>			m_internal_vertices;
		IVertex *						m_internal_vertices_buffer_cursor;

		std::vector<T>					m_internal_raw_vertices;
		
		std::vector<K>					m_internal_index;

		Ref<Shaders::Shader>			m_shader;
		Ref<Buffers::VertexBuffer<T>>	m_vertex_buffer;
		Ref<Buffers::VertexArray<T, K>> m_vertex_array;
		Ref<Buffers::IndexBuffer<K>>	m_index_buffer;

	};


	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage()
		:		
		M_MAX_QUAD(1000),
		M_MAX_VERTICES_PER_QUAD(4),
		M_MAX_INDEX_PER_QUAD(6),
		M_MAX_INDICES(M_MAX_INDEX_PER_QUAD * M_MAX_QUAD),
		m_internal_vertices(M_MAX_VERTICES_PER_QUAD * M_MAX_QUAD),
		m_internal_vertices_buffer_cursor(nullptr),
		m_internal_raw_vertices(M_MAX_VERTICES_PER_QUAD * M_MAX_QUAD),
		m_internal_index(M_MAX_INDICES, 0),
		m_shader(nullptr),
		m_vertex_buffer(new Buffers::VertexBuffer<T>(M_MAX_VERTICES_PER_QUAD * M_MAX_QUAD)),
		m_index_buffer(new Buffers::IndexBuffer<K>()), 
		m_vertex_array(new Buffers::VertexArray<T, K>())
	{
		m_internal_vertices_buffer_cursor = m_internal_vertices.data();

		size_t offset = 0;
		for (size_t i = 0; i < M_MAX_INDICES; i += 6)
		{
			m_internal_index[i + 0] = 0 + offset;
			m_internal_index[i + 1] = 1 + offset;
			m_internal_index[i + 2] = 2 + offset;
			m_internal_index[i + 3] = 2 + offset;
			m_internal_index[i + 4] = 3 + offset;
			m_internal_index[i + 5] = 0 + offset;

			offset += 4;
		}
	}														    
}

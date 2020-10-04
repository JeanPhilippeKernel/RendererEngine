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
			m_vertex_buffer->SetLayout(buffer_layout);
		}

		void AddVertices(const std::vector<GraphicVertex>& vertices) {
			m_internal_vertices.insert(std::end(m_internal_vertices), std::begin(vertices), std::end(vertices));

			std::for_each(std::begin(vertices), std::end(vertices), 
				[this] (const GraphicVertex& vertex) { 
					auto& data =  vertex.GetData();
					m_internal_raw_vertices.insert(std::end(m_internal_raw_vertices), std::begin(data), std::end(data));
				});
		}
		
		void UpdateBuffers() {
			//ToDo: update indexes buffer count
			std::vector<int> index = {-4, -3, -2, -2, -1, -4};

			size_t length =  m_internal_vertices.size();
			for (size_t i = 0; i < length; i += 4)
			{
				index[0] = index[0] + 4;
				index[1] = index[1] + 4;
				index[2] = index[2] + 4;
				index[3] = index[3] + 4;
				index[4] = index[4] + 4;
				index[5] = index[5] + 4;
			
				m_internal_index.insert(std::end(m_internal_index), std::begin(index), std::end(index));
			}

			 m_vertex_buffer->SetData(m_internal_raw_vertices);
			 m_index_buffer->SetData(m_internal_index);		

			 m_vertex_array->SetIndexBuffer(m_index_buffer);
			 m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		}

		void FlushBuffers() {
			m_internal_vertices.clear();
			m_internal_raw_vertices.clear();
			m_internal_index.clear();
		}


		Ref<Buffers::VertexArray<T, K>> GetVertexArray() { return m_vertex_array; }
		Ref<Shaders::Shader> GetShader() { return m_shader; }


	private:
		std::vector<GraphicVertex>		m_internal_vertices;
		std::vector<T>					m_internal_raw_vertices;
		std::vector<K>					m_internal_index;
		
		Ref<Shaders::Shader>			m_shader;
		Ref<Buffers::VertexBuffer<T>>	m_vertex_buffer;
		Ref<Buffers::VertexArray<T, K>> m_vertex_array;
		Ref<Buffers::IndexBuffer<K>>	m_index_buffer;

		unsigned int			M_MAX_VERTICES{1000};
	};


	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage()
		:
		m_internal_vertices(),
		m_internal_raw_vertices(),
		m_internal_index(),
		m_shader(nullptr),
		m_vertex_buffer(new Buffers::VertexBuffer<T>()),
		m_index_buffer(new Buffers::IndexBuffer<K>()), 
		m_vertex_array(new Buffers::VertexArray<T, K>())
	{
	}														    
}

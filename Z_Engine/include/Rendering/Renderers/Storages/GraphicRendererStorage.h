#pragma once
#include <vector>
#include <algorithm>
#include <numeric>

#include "GraphicVertex.h"
#include "../../Buffers/VertexArray.h"
#include "../../Buffers/VertexBuffer.h"
#include "../../Buffers/IndexBuffer.h"
#include "../../Shaders/Shader.h"


#include "../../Materials/ShaderMaterial.h"


namespace Z_Engine::Rendering::Renderers::Storages {
	
	template <typename T, typename K>
	class GraphicRendererStorage {
	public:
		explicit GraphicRendererStorage(
			const Ref<Shaders::Shader>& shader, 
			const std::vector<Ref<Buffers::VertexBuffer<T>>>& vertex_buffer_list,
			const Ref<Materials::ShaderMaterial>& material
		);
		~GraphicRendererStorage() = default;

		void SetShader(const Ref<Shaders::Shader>& shader) {
			m_shader = shader;
		}

		const Ref<Shaders::Shader>& GetShader() const { 
			return m_shader; 
		}

		const Ref<Buffers::VertexArray<T, K>>& GetVertexArray() const { 
			return m_vertex_array; 
		}

		const Ref<Materials::ShaderMaterial>& GetMaterial() const {
			return m_shader_material;
		}

	private:

		Ref<Materials::ShaderMaterial>			m_shader_material;

		std::vector<T>							m_internal_raw_vertices;
		std::vector<K>							m_internal_index;

		Ref<Shaders::Shader>					m_shader;
		Ref<Buffers::VertexBuffer<T>>			m_vertex_buffer;
		Ref<Buffers::VertexArray<T, K>>			m_vertex_array;
		Ref<Buffers::IndexBuffer<K>>			m_index_buffer;
	};

	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage(
		const Ref<Shaders::Shader>& shader,
		const std::vector<Ref<Buffers::VertexBuffer<T>>>& vertex_buffer_list,
		const Ref<Materials::ShaderMaterial>& material)
		:
		m_shader(shader),
		m_shader_material(material),
		m_vertex_buffer(nullptr),
		m_index_buffer(nullptr),
		m_vertex_array(nullptr)
	{
		unsigned int vertex_count = std::accumulate(
			std::begin(vertex_buffer_list), std::end(vertex_buffer_list),
			0, [](unsigned int init, const Ref<Buffers::VertexBuffer<T>>& buffer) { return std::move(init) + buffer->GetVertexCount(); });

		m_internal_raw_vertices						= std::vector<T>(vertex_count * (sizeof(Renderers::Storages::IVertex) / sizeof(float)), 0.0f);
		T* m_internal_raw_vertices_buffer_cursor	= m_internal_raw_vertices.data();

		std::for_each(std::begin(vertex_buffer_list), std::end(vertex_buffer_list), [this, &m_internal_raw_vertices_buffer_cursor](const Ref<Buffers::VertexBuffer<T>>& buffer) {
			const std::vector<T>& data	= buffer->GetData();
			const size_t byte_size		= buffer->GetByteSize();

			std::memcpy(m_internal_raw_vertices_buffer_cursor, &data[0], byte_size);
			m_internal_raw_vertices_buffer_cursor += data.size();
		});


		m_vertex_buffer.reset(new Buffers::VertexBuffer<T>(vertex_count));
		m_vertex_buffer->SetLayout(Renderers::Storages::GraphicVertex::Descriptor::GetLayout());
		m_vertex_buffer->SetData(m_internal_raw_vertices);

		unsigned int index_count	= (vertex_count * 6) / 4;  // As 4-vertices means 6 indices
		m_internal_index			= std::vector<K>(index_count, 0);

		size_t offset = 0;
		for (size_t i = 0; i < index_count; i += 6) 
		{
			m_internal_index[i + 0] = 0 + offset;
			m_internal_index[i + 1] = 1 + offset;
			m_internal_index[i + 2] = 2 + offset;
			m_internal_index[i + 3] = 2 + offset;
			m_internal_index[i + 4] = 3 + offset;
			m_internal_index[i + 5] = 0 + offset;

			offset += 4;
		}
		m_index_buffer.reset(new Buffers::IndexBuffer<K>());
		m_index_buffer->SetData(m_internal_index);


		m_vertex_array.reset(new Buffers::VertexArray<T, K>());
		m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		m_vertex_array->SetIndexBuffer(m_index_buffer);
	}
}

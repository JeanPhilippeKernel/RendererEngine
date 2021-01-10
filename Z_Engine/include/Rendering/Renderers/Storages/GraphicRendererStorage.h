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


	enum class GraphicRendererStorageType
	{
		GRAPHIC_2D_STORAGE_TYPE,
		GRAPHIC_3D_STORAGE_TYPE
	};

	
	template <typename T, typename K>
	class GraphicRendererStorage {
	public:
		explicit GraphicRendererStorage(
			const Ref<Shaders::Shader>&							shader, 
			const std::vector<Ref<Buffers::VertexBuffer<T>>>&	vertex_buffer_list,
			const Ref<Materials::ShaderMaterial>&				material,
			GraphicRendererStorageType							storage_type
		);

		explicit GraphicRendererStorage(
			const Ref<Shaders::Shader>&								shader,
			const std::vector<Renderers::Storages::GraphicVertex>&	vertices_list,
			const Ref<Materials::ShaderMaterial>&					material,
			GraphicRendererStorageType								storage_type
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

		Ref<Shaders::Shader>					m_shader			{ nullptr };
		Ref<Materials::ShaderMaterial>			m_shader_material	{ nullptr };
		
		Ref<Buffers::VertexBuffer<T>>			m_vertex_buffer		{ nullptr };
		Ref<Buffers::IndexBuffer<K>>			m_index_buffer		{ nullptr };
		Ref<Buffers::VertexArray<T, K>>			m_vertex_array		{ nullptr };
	};

	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage(
		const Ref<Shaders::Shader>&							shader,
		const std::vector<Ref<Buffers::VertexBuffer<T>>>&	vertex_buffer_list,
		const Ref<Materials::ShaderMaterial>&				material,
		GraphicRendererStorageType							storage_type
	)
		:
		m_shader(shader),
		m_shader_material(material)
	{
		unsigned int vertex_count = std::accumulate(
			std::begin(vertex_buffer_list), std::end(vertex_buffer_list),
			0, [](unsigned int init, const Ref<Buffers::VertexBuffer<T>>& buffer) { return std::move(init) + buffer->GetVertexCount(); });

		std::vector<T> raw_vertices		= std::vector<T>(vertex_count * (sizeof(IVertex) / sizeof(float)), 0.0f);
		T* raw_vertices_buffer_cursor	= raw_vertices.data();

		std::for_each(std::begin(vertex_buffer_list), std::end(vertex_buffer_list), [&raw_vertices_buffer_cursor](const Ref<Buffers::VertexBuffer<T>>& buffer) {
			const std::vector<T>& data	= buffer->GetData();
			const size_t byte_size		= buffer->GetByteSize();

			std::memcpy(raw_vertices_buffer_cursor, &data[0], byte_size);
			raw_vertices_buffer_cursor += data.size();
		});


		m_vertex_buffer.reset(new Buffers::VertexBuffer<T>(vertex_count));
		m_vertex_buffer->SetLayout(Renderers::Storages::GraphicVertex::Descriptor::GetLayout());
		m_vertex_buffer->SetData(raw_vertices);

		unsigned int index_count{ 0 };
		std::vector<K> raw_indexes;
		size_t offset = 0;


		if (storage_type == GraphicRendererStorageType::GRAPHIC_2D_STORAGE_TYPE) {
			index_count = (vertex_count * 6) / 4;  // As 4-vertices means 6 indices
			raw_indexes = std::vector<K>(index_count, 0);

			for (size_t i = 0; i < index_count; i += 6) {
				raw_indexes[i + 0] = 0 + offset;
				raw_indexes[i + 1] = 1 + offset;
				raw_indexes[i + 2] = 2 + offset;
				raw_indexes[i + 3] = 2 + offset;
				raw_indexes[i + 4] = 3 + offset;
				raw_indexes[i + 5] = 0 + offset;

				offset += 4;
			}
		}

		else {
			index_count = (vertex_count * 3) / 3;  // As 3-vertices means 3 indices
			raw_indexes = std::vector<K>(index_count, 0);
			for (size_t i = 0; i < index_count; i += 3) {
				raw_indexes[i + 0] = 0 + offset;
				raw_indexes[i + 1] = 1 + offset;
				raw_indexes[i + 2] = 2 + offset;

				offset += 3;
			}
		}

		m_index_buffer.reset(new Buffers::IndexBuffer<K>());
		m_index_buffer->SetData(raw_indexes);


		m_vertex_array.reset(new Buffers::VertexArray<T, K>());
		m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		m_vertex_array->SetIndexBuffer(m_index_buffer);
	}


	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage(
		const Ref<Shaders::Shader>&				shader,
		const std::vector<GraphicVertex>&		vertices_list,
		const Ref<Materials::ShaderMaterial>&	material,
		GraphicRendererStorageType				storage_type
	)
		:
		m_shader(shader),
		m_shader_material(material)
	{

		unsigned int vertex_count = static_cast<unsigned int>(vertices_list.size());
		std::vector<T> raw_vertices;

		std::for_each(std::begin(vertices_list), std::end(vertices_list), [&raw_vertices](const GraphicVertex& vertex) {
			const auto& data = vertex.GetData();
			std::copy(std::begin(data), std::end(data), std::back_inserter(raw_vertices));
		});

		m_vertex_buffer.reset(new Buffers::VertexBuffer<T>(vertex_count));
		m_vertex_buffer->SetLayout(GraphicVertex::Descriptor::GetLayout());
		m_vertex_buffer->SetData(raw_vertices);

		unsigned int index_count{ 0 };
		std::vector<K> raw_indexes;
		size_t offset = 0;


		if (storage_type == GraphicRendererStorageType::GRAPHIC_2D_STORAGE_TYPE) {
			index_count = (vertex_count * 6) / 4;  // As 4-vertices means 6 indices
			raw_indexes = std::vector<K>(index_count, 0);

			for (size_t i = 0; i < index_count; i += 6) {
				raw_indexes[i + 0] = 0 + offset;
				raw_indexes[i + 1] = 1 + offset;
				raw_indexes[i + 2] = 2 + offset;
				raw_indexes[i + 3] = 2 + offset;
				raw_indexes[i + 4] = 3 + offset;
				raw_indexes[i + 5] = 0 + offset;

				offset += 4;
			}
		}

		else {
			index_count = (vertex_count * 3) / 3;  // As 3-vertices means 3 indices
			raw_indexes = std::vector<K>(index_count, 0);
			for (size_t i = 0; i < index_count; i += 3) {
				raw_indexes[i + 0] = 0 + offset;
				raw_indexes[i + 1] = 1 + offset;
				raw_indexes[i + 2] = 2 + offset;

				offset += 3;
			}
		}

		m_index_buffer.reset(new Buffers::IndexBuffer<K>());
		m_index_buffer->SetData(raw_indexes);

		m_vertex_array.reset(new Buffers::VertexArray<T, K>());
		m_vertex_array->AddVertexBuffer(m_vertex_buffer);
		m_vertex_array->SetIndexBuffer(m_index_buffer);
	}


}

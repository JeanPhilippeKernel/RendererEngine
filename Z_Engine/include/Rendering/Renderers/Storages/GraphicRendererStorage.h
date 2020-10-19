#pragma once
#include <vector>
#include <array>
#include <algorithm>

#include "GraphicVertex.h"
#include "../../Buffers/VertexArray.h"
#include "../../Buffers/VertexBuffer.h"
#include "../../Buffers/IndexBuffer.h"
#include "../../Shaders/Shader.h"

#include "../RenderCommand.h"
#include "../../Textures/Texture.h"
#include "../../Meshes/Mesh.h"
#include "../../../Managers/TextureManager.h"

namespace Z_Engine::Rendering::Renderers::Storages {
	
	template <typename T, typename K>
	class GraphicRendererStorage {
	public:
		GraphicRendererStorage(Ref<Managers::TextureManager>& texture_manager);
		~GraphicRendererStorage() = default;

		void SetShader(const Ref<Shaders::Shader>& shader) { 
			m_shader = shader; 
		}
		
		void SetViewProjectionMatrix(const glm::mat4& matrix)  {
			m_view_projection = matrix;
		}
		
		void SetVertexBufferLayout(std::initializer_list<Buffers::Layout::ElementLayout<T>>&& element_layout ) {
			Buffers::Layout::BufferLayout<T> buffer_layout{element_layout};
			m_vertex_buffer->SetLayout(std::move(buffer_layout));
		}

		void AddMesh(const Rendering::Meshes::Mesh& mesh) {

			 const auto& material =  mesh.GetMaterial();
			 const auto& texture =  material->GetTexture();

			 bool already_in_slot =  false;
			 int found_at_slot = 0;
			 
			 for(size_t x = 0 ; x < m_texture_slot_unit_cursor; ++x) {
				already_in_slot = (*m_texture_slot_unit[x] == *texture);
				if(already_in_slot) {
					found_at_slot = x;	
					break;
				}
			 }

			 if(!already_in_slot) 
			 { 
				 m_texture_slot_unit[m_texture_slot_unit_cursor] = texture;
				 found_at_slot = m_texture_slot_unit_cursor; 
				 m_texture_slot_unit_cursor++;
			 } 


			 const auto& geometry =  mesh.GetGeometry();
			 auto& vertices = geometry->GetVertices();

			 std::for_each(std::begin(vertices), std::end(vertices), 
				 [this, &found_at_slot] (Renderers::Storages::GraphicVertex& vertex) {
					vertex.SetTextureId((float)found_at_slot);
				 });

			AddVertices(vertices);

		}

		void StartBacth() {			
			m_internal_raw_vertices_buffer_cursor	= &m_internal_raw_vertices[0]; // reset the position of the cursor
			m_texture_slot_unit_cursor = 1; // slot 0 is reserved;
			std::memset(&m_internal_raw_vertices[0], 0, m_internal_raw_vertices.size() * sizeof(T));
		}

		void EndBatch() {
			m_vertex_buffer->SetData(m_internal_raw_vertices);
			m_vertex_array->AddVertexBuffer(m_vertex_buffer);

			for (int x = 0; x < m_texture_slot_unit_cursor; ++x) {
				m_texture_slot_unit[x]->Bind(x);
			}

			this->Flush(m_shader, m_vertex_array);
		}


		Ref<Buffers::VertexArray<T, K>>& GetVertexArray() { 
			return m_vertex_array; 
		}

		Ref<Shaders::Shader>& GetShader() { 
			return m_shader; 
		}


	private:
		unsigned int					M_MAX_QUAD;
		unsigned int					M_MAX_VERTICES_PER_QUAD;
		unsigned int					M_MAX_INDEX_PER_QUAD;
		unsigned int					M_MAX_INDICES;
		
		glm::mat4						m_view_projection;

		std::vector<T>					m_internal_raw_vertices;
		T*								m_internal_raw_vertices_buffer_cursor;
		
		std::vector<K>					m_internal_index;

		std::array<Ref<Textures::Texture>, 32>	m_texture_slot_unit;
		int								m_texture_slot_unit_cursor;

		Ref<Shaders::Shader>			m_shader;
		Ref<Buffers::VertexBuffer<T>>	m_vertex_buffer;
		Ref<Buffers::VertexArray<T, K>> m_vertex_array;
		Ref<Buffers::IndexBuffer<K>>	m_index_buffer;

		Ref<Managers::TextureManager>&	m_texture_manager;


	private:
		void AddVertices(const std::vector<GraphicVertex>& vertices) {
			
			const auto distance = std::distance(&m_internal_raw_vertices[0], m_internal_raw_vertices_buffer_cursor);
			if(
				(distance >= m_internal_raw_vertices.size())  
				|| (m_texture_slot_unit_cursor >= m_texture_slot_unit.size())
				) 
			{
				// Todo :  Flush buffer and immediate draw...
				EndBatch();
				StartBacth();
			}

			std::for_each(std::begin(vertices), std::end(vertices), [this](const GraphicVertex& vertex) {
				const auto& raw_buffer	=  vertex.GetData();
				size_t buffer_size		=  raw_buffer.size();

				std::memcpy(m_internal_raw_vertices_buffer_cursor, &raw_buffer[0], buffer_size * sizeof(T));
				m_internal_raw_vertices_buffer_cursor += buffer_size;
			});

		}
		
		void Flush(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array);

	};


	template<typename T, typename K>
	inline GraphicRendererStorage<T, K>::GraphicRendererStorage(Ref<Managers::TextureManager>& texture_manager)
		:		
		m_texture_manager(texture_manager),

		M_MAX_QUAD(10),	// this value, when higher drop the framerate... need to investigate 
		M_MAX_VERTICES_PER_QUAD(4),
		M_MAX_INDEX_PER_QUAD(6),
		M_MAX_INDICES(M_MAX_INDEX_PER_QUAD * M_MAX_QUAD),
		m_view_projection(1.0f),
		m_internal_raw_vertices(M_MAX_VERTICES_PER_QUAD * M_MAX_QUAD * (sizeof(IVertex) / sizeof(T))),
		m_internal_raw_vertices_buffer_cursor(nullptr),
		m_internal_index(M_MAX_INDICES, 0),
		m_texture_slot_unit(),
		m_texture_slot_unit_cursor(0),
		m_shader(nullptr),
		m_vertex_buffer(new Buffers::VertexBuffer<T>(M_MAX_VERTICES_PER_QUAD * M_MAX_QUAD)),
		m_index_buffer(new Buffers::IndexBuffer<K>()), 
		m_vertex_array(new Buffers::VertexArray<T, K>())
	{

		m_internal_raw_vertices_buffer_cursor = m_internal_raw_vertices.data();

		texture_manager->Add("__reserved_texture__", 1, 1);
		m_texture_slot_unit[m_texture_slot_unit_cursor] = texture_manager->Obtains("__reserved_texture__");
		  

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

		m_index_buffer->SetData(m_internal_index);
		m_vertex_array->SetIndexBuffer(m_index_buffer);
	}		


	template<typename T, typename K>
	inline void  GraphicRendererStorage<T, K>::Flush(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
		shader->SetUniform("uniform_viewprojection", m_view_projection);
		RendererCommand::DrawIndexed(shader, vertex_array);
	}
}

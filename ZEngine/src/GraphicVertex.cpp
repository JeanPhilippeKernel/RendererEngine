#include <Rendering/Renderers/Storages/GraphicVertex.h>


namespace ZEngine::Rendering::Renderers::Storages {
	GraphicVertex::GraphicVertex()
		:IVertex(), m_buffer()
	{
		_UpdateBuffer();
	}

	GraphicVertex::GraphicVertex(
		const glm::vec3& position,
		const glm::vec2& texture_coord
	)
		: IVertex() 
	{
		m_position			= position; 
		m_texture_coord		= texture_coord;

		_UpdateBuffer();
	}

	glm::vec3 GraphicVertex::GetPosition() const { 
		return m_position; 
	}
	
	glm::vec2 GraphicVertex::GetTextureCoord() const { 
		return m_texture_coord; 
	}

	void GraphicVertex::SetPosition(const glm::vec3& value) { 
		m_position	= value; 
		m_buffer[0] = m_position.x;
		m_buffer[1] = m_position.y;
		m_buffer[2] = m_position.z;
	}

	void GraphicVertex::SetTextureCoord(const glm::vec2& value) { 
		m_texture_coord = value; 
		m_buffer[3]		= m_texture_coord.x;
		m_buffer[4]		= m_texture_coord.y;
	}

	void GraphicVertex::ApplyMatrixToPosition(const glm::mat4& matrix) {
		glm::vec4 position =  glm::vec4(m_position, 1.0f);
		position = matrix * position;
		SetPosition({position.x, position.y, position.z});
	}

	void GraphicVertex::_UpdateBuffer() {
		
		m_buffer[0]		= m_position.x;
		m_buffer[1]		= m_position.y;
		m_buffer[2]		= m_position.z;
		
		m_buffer[3]		= m_texture_coord.x;
		m_buffer[4]	= m_texture_coord.y;
		
	}


	Buffers::Layout::BufferLayout<float> GraphicVertex::Descriptor::m_internal_layout {
		{
			Buffers::Layout::ElementLayout<float>(3, "position"),
			Buffers::Layout::ElementLayout<float>(2, "texture_coord")
		}
	};

}
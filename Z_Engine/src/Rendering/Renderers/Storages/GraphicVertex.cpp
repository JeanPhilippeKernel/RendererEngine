#include "GraphicVertex.h"


namespace Z_Engine::Rendering::Renderers::Storages {
	GraphicVertex::GraphicVertex()
		:IVertex(), m_buffer()
	{
		std::memset(&m_buffer[0], 0, m_buffer.size());
		_UpdateBuffer();
	}

	GraphicVertex::GraphicVertex(const glm::vec3& position, const glm::vec4& color, const glm::vec2& texture_coord, float texture_id)
		:IVertex() 
	{
		m_position		= position; 
		m_color			= color; 
		m_texture_coord	= texture_coord;
		m_texture_id	= texture_id;

		_UpdateBuffer();
	}

	glm::vec3 GraphicVertex::GetPosition() const { return m_position; }
	glm::vec4 GraphicVertex::GetColor() const { return m_color; }
	glm::vec2 GraphicVertex::GetTextureCoord() const { return m_texture_coord; }

	float GraphicVertex::GetTextureId() const { return m_texture_id; }


	void GraphicVertex::SetPosition(const glm::vec3& value) { 
		m_position = value; 
		m_buffer[0] = m_position.x;
		m_buffer[1] = m_position.y;
		m_buffer[2] = m_position.z;
	}

	void GraphicVertex::SetColor(const glm::vec4& value) { 
		m_color = value; 
		m_buffer[3] = m_color.x;
		m_buffer[4] = m_color.y;
		m_buffer[5] = m_color.z;
		m_buffer[6] = m_color.w;
	}
	void GraphicVertex::SetTextureCoord(const glm::vec2& value) { 
		m_texture_coord = value; 
		m_buffer[7] = m_texture_coord.x;
		m_buffer[8] = m_texture_coord.y;
	}

	void GraphicVertex::SetTextureId(float value) { 
		m_texture_id =  value;
		m_buffer[9] = m_texture_id;
	}

	void GraphicVertex::_UpdateBuffer() {
		m_buffer[0] = m_position.x;
		m_buffer[1] = m_position.y;
		m_buffer[2] = m_position.z;
		
		m_buffer[3] = m_color.x;
		m_buffer[4] = m_color.y;
		m_buffer[5] = m_color.z;
		m_buffer[6] = m_color.w;
		
		m_buffer[7] = m_texture_coord.x;
		m_buffer[8] = m_texture_coord.y;
		m_buffer[9] = m_texture_id;
	}

}
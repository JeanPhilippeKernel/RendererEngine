#include "GraphicVertex.h"


namespace Z_Engine::Rendering::Renderers::Storages {
	GraphicVertex::GraphicVertex()
		:IVertex(), m_buffer(10)
	{
		_UpdateBuffer();
	}

	GraphicVertex::GraphicVertex(const glm::vec3 & position, const glm::vec4 & color, const glm::vec3 & texture)
		:IVertex(), m_buffer(10) 
	{
		m_position	= position; 
		m_color		= color; 
		m_texture	= texture;

		_UpdateBuffer();
	}

	glm::vec3 GraphicVertex::GetPosition() const { return m_position; }
	glm::vec4 GraphicVertex::GetColor() const { return m_color; }
	glm::vec3 GraphicVertex::GetTexture() const { return m_texture; }


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
	void GraphicVertex::SetTexture(const glm::vec3& value) { 
		m_texture = value; 
		m_buffer[7] = m_texture.x;
		m_buffer[8] = m_texture.y;
		m_buffer[9] = m_texture.z;
	}

	void GraphicVertex::_UpdateBuffer() {
		m_buffer[0] = m_position.x;
		m_buffer[1] = m_position.y;
		m_buffer[2] = m_position.z;
		
		m_buffer[3] = m_color.x;
		m_buffer[4] = m_color.y;
		m_buffer[5] = m_color.z;
		m_buffer[6] = m_color.w;
		
		m_buffer[7] = m_texture.x;
		m_buffer[8] = m_texture.y;
		m_buffer[9] = m_texture.z;
	}

}
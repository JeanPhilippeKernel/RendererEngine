#include "GraphicVertex.h"


namespace Z_Engine::Rendering::Renderers::Storages {
	GraphicVertex::GraphicVertex()
		:IVertex(), m_buffer()
	{
		std::memset(&m_buffer[0], 0, m_buffer.size());
		_UpdateBuffer();
	}

	GraphicVertex::GraphicVertex(
		const glm::vec3& position, const glm::vec4& color,
		float texture_id, const glm::vec2& texture_coord,
		const glm::vec4& texture_tint_color, float texture_tiling_factor
	)
		: IVertex() 
	{
		m_position		= position; 
		m_color			= color; 
		
		m_texture_id	= texture_id;
		m_texture_coord	= texture_coord;
		m_texture_tiling_factor = texture_tiling_factor;
		m_texture_tint_color = texture_tint_color;

		_UpdateBuffer();
	}

	glm::vec3 GraphicVertex::GetPosition() const { return m_position; }
	glm::vec4 GraphicVertex::GetColor() const { return m_color; }

	float GraphicVertex::GetTextureId() const { return m_texture_id; }
	float GraphicVertex::GetTextureTilingFactor() const { return m_texture_tiling_factor; }
	glm::vec2 GraphicVertex::GetTextureCoord() const { return m_texture_coord; }
	glm::vec4 GraphicVertex::GetTextureTintColor() const { return m_texture_tint_color; }

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

	void GraphicVertex::SetTextureId(float value) { 
		m_texture_id =  value;
		m_buffer[7] = m_texture_id;
	}

	void GraphicVertex::SetTextureCoord(const glm::vec2& value) { 
		m_texture_coord = value; 
		m_buffer[8] = m_texture_coord.x;
		m_buffer[9] = m_texture_coord.y;
	}


	void GraphicVertex::SetTextureTilingFactor(float value) {
		  m_texture_tiling_factor = value;
		  m_buffer[10] = m_texture_tiling_factor;
	}

	void GraphicVertex::SetTextureTintColor(const glm::vec4& value) {
		m_texture_tint_color = value;
		m_buffer[11] = m_texture_tint_color.x;
		m_buffer[12] = m_texture_tint_color.y;
		m_buffer[13] = m_texture_tint_color.z;
		m_buffer[14] = m_texture_tint_color.w;
		
	}


	void GraphicVertex::ApplyMatrixToPosition(const glm::mat4& matrix) {
		glm::vec4 position =  glm::vec4(m_position, 1.0f);
		position = matrix * position;
		SetPosition({position.x, position.y, position.z});
	}

	void GraphicVertex::_UpdateBuffer() {
		m_buffer[0] = m_position.x;
		m_buffer[1] = m_position.y;
		m_buffer[2] = m_position.z;
		
		m_buffer[3] = m_color.x;
		m_buffer[4] = m_color.y;
		m_buffer[5] = m_color.z;
		m_buffer[6] = m_color.w;

		m_buffer[7] = m_texture_id;
		
		m_buffer[8] = m_texture_coord.x;
		m_buffer[9] = m_texture_coord.y;
		
		m_buffer[10] = m_texture_tiling_factor;
		
		m_buffer[11] = m_texture_tint_color.x;
		m_buffer[12] = m_texture_tint_color.y;
		m_buffer[13] = m_texture_tint_color.z;
		m_buffer[14] = m_texture_tint_color.w;
	}

}
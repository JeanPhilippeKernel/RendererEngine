#pragma once
#include <glm/glm.hpp>

namespace Z_Engine::Rendering::Renderers::Storages {

	struct IVertex {
		glm::vec3	m_position				{0.0f, 0.0f, 0.0f};
		glm::vec4	m_color					{0.0f, 0.0f, 0.0f, 0.0f};
		
		float		m_texture_id			{0.0f}; 
		glm::vec2	m_texture_coord			{0.0f, 0.0f};
		float		m_texture_tiling_factor	{0.0f};
		glm::vec4	m_texture_tint_color	{0.0f, 0.0f, 0.0f, 0.0f};
	};
}

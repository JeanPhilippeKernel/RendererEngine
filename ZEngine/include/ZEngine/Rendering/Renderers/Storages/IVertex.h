#pragma once
#include <glm/glm/glm.hpp>

namespace ZEngine::Rendering::Renderers::Storages {

	struct IVertex {
		glm::vec3	m_position				{0.0f, 0.0f, 0.0f};
		glm::vec2	m_texture_coord			{0.0f, 0.0f};
	};
}

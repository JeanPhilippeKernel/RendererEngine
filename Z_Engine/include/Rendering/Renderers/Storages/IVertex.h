#pragma once
#include "../../../dependencies/glm/glm.hpp"

namespace Z_Engine::Rendering::Renderers::Storages {

	struct IVertex {
		glm::vec3	m_position				{0.0f, 0.0f, 0.0f};
		glm::vec2	m_texture_coord			{0.0f, 0.0f};
	};
}

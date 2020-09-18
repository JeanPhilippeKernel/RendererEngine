#pragma once
#include <glm/glm.hpp>

namespace Z_Engine::Rendering::Renderers::Storages {

	struct IVertex {
		IVertex() =  default;
		~IVertex() = default;

		glm::vec3 m_position	{0.0f, 0.0f, 0.0f};
		glm::vec4 m_color		{0.0f, 0.0f, 0.0f, 0.0f};
		glm::vec3 m_texture		{0.0f, 0.0f, 0.0f}; 	 // the Z component represent the texture ID
	};
}

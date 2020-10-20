#include "StandardMaterial.h"

namespace Z_Engine::Rendering::Materials {
	
	StandardMaterial::StandardMaterial() 
		:
		ShaderMaterial("Assets/Shaders/simple_mesh_2d.glsl", {"texture_tiling_factor", "texture_tint_color"})
	{
	}

	void StandardMaterial::SetAttributes() {
		m_uniform_collection["texture_tiling_factor"] = 1.0f;
		m_uniform_collection["texture_tint_color"] = glm::vec4(1.0f);
	}
} 

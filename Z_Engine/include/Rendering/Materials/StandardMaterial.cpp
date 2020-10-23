#include "StandardMaterial.h"

namespace Z_Engine::Rendering::Materials {
	
	bool StandardMaterial::m_already_initialized =  false;


	StandardMaterial::StandardMaterial() 
		:
		ShaderMaterial("src/Assets/Shaders/simple_mesh_2d.glsl", {"texture_tiling_factor", "texture_tint_color"})
	{
		m_material_name =  typeid(*this).name();
		

		if(!m_already_initialized) {
			
			int texture_slot[32] = {0};

			for(int x = 0; x < 32; ++x) texture_slot[x] = x;
			m_shader->SetUniform("uniform_texture_slot", texture_slot , 32);

			m_already_initialized =  true;
		}
	}

	void StandardMaterial::SetAttributes() {
		m_uniform_collection["texture_tiling_factor"] = 1.0f;
		m_uniform_collection["texture_tint_color"] = glm::vec4(1.0f);
	}
} 

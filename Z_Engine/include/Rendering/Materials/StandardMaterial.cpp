#include "StandardMaterial.h"
#include "../Meshes/Mesh.h"
#include "../../dependencies/fmt/include/fmt/format.h"

namespace Z_Engine::Rendering::Materials {

	StandardMaterial::StandardMaterial() 
		: 
		ShaderMaterial("src/Assets/Shaders/simple_mesh_2d.glsl"), 
		m_tile_factor(1.0f),
		m_tint_color(glm::vec4(1.0f))
	{
		m_unique_identifier = "3460B305-3F25-493E-961B-9C837F00369E";
		m_material_name =  typeid(*this).name();
	}

	unsigned int StandardMaterial::GetHashCode()
	{
		auto hash = static_cast<unsigned int>(m_tile_factor) 
			^ static_cast<unsigned int>(m_tint_color.x)
			^ static_cast<unsigned int>(m_tint_color.y)
			^ static_cast<unsigned int>(m_tint_color.z);

		return hash ^ ShaderMaterial::GetHashCode();
	}

	void StandardMaterial::SetTileFactor(float value) {
		m_tile_factor = value;
	}
	
	void StandardMaterial::SetTintColor(const glm::vec4& value) {
		m_tint_color = value;
	}


	void StandardMaterial::Apply() {
		m_shader->SetUniform("material.tiling_factor", m_tile_factor);
		m_shader->SetUniform("material.tint_color", m_tint_color);
		m_texture->Bind();
	}
} 

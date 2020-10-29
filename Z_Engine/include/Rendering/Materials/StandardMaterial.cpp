#include "StandardMaterial.h"
#include "../Meshes/Mesh.h"
#include "../../dependencies/fmt/include/fmt/format.h"

namespace Z_Engine::Rendering::Materials {

	StandardMaterial::StandardMaterial() 
		:
		ShaderMaterial(
			"src/Assets/Shaders/simple_mesh_2d.glsl", 
			{"texture_tiling_factor", "texture_tint_color"}
		), 
		m_tile_factor(1.0f),
		m_tint_color(glm::vec4(1.0f))
	{
		m_material_name =  typeid(*this).name();
		m_uniform_collection["texture_tiling_factor"]	= m_tile_factor;
		m_uniform_collection["texture_tint_color"]		= m_tint_color;
		
		std::function<void()> fn = [this]() {
			const int array_size =  32;
			int texture_slot[array_size] = {0};
			for(int x = 0; x < array_size; ++x) texture_slot[x] = x;
			
			m_shader->SetUniform("uniform_texture_slot", texture_slot , array_size);
		};

		InitDefaultUniforms(fn);
	}

	void StandardMaterial::SetTileFactor(float value) {
		m_tile_factor									= value;
		m_uniform_collection["texture_tiling_factor"]	= m_tile_factor;
	}
	
	void StandardMaterial::SetTintColor(const glm::vec4& value) {
		m_tint_color								= value;
		m_uniform_collection["texture_tint_color"]	= m_tint_color;
	}


	void StandardMaterial::SetAttributes() {

		if(!m_attribute_already_set) {
			assert(m_owner_mesh != nullptr);

			auto _tile_factor	=  std::any_cast<float>(m_uniform_collection["texture_tiling_factor"]);
			auto _tint_color	=  std::any_cast<glm::vec4>(m_uniform_collection["texture_tint_color"]);

			unsigned int index			= m_owner_mesh->GetIdentifier();
			auto uniform_array_name		= fmt::format("texture_tiling_factor_{0}", index);
			auto uniform_array_name_1	= fmt::format("texture_tint_color_{0}", index);

			m_shader->SetUniform(uniform_array_name.c_str(), _tile_factor);
			m_shader->SetUniform(uniform_array_name_1.c_str(), _tint_color);

			m_attribute_already_set = true;
		}
	}
} 

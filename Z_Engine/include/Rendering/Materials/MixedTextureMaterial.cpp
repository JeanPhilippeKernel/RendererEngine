#include "MixedTextureMaterial.h"
#include "../Meshes/Mesh.h"
#include "../../dependencies/fmt/include/fmt/format.h"

namespace Z_Engine::Rendering::Materials {

	MixedTextureMaterial::MixedTextureMaterial() 
		: ShaderMaterial("src/Assets/Shaders/mixed_texture.glsl", {"interpolate_factor"})
	{
		m_material_name = typeid(*(this)).name();
		m_uniform_collection["interpolate_factor"] =  m_interpolate_factor;

		auto init_fn = [this]() {
			const int array_size =  32;
			int texture_slot[array_size] = {0};
			for(int x = 0; x < array_size; ++x) texture_slot[x] = x;
			
			m_shader->SetUniform("uniform_texture_slot", texture_slot , array_size);
		};

		InitDefaultUniforms(init_fn);
	}



	void MixedTextureMaterial::SetSecondTexture(Ref<Textures::Texture>& texture) {
		m_second_texture = texture;
	}
	
	void MixedTextureMaterial::SetSecondTexture(Textures::Texture*  const texture) {
		m_second_texture.reset(texture);
	}

	void MixedTextureMaterial::SetInterpolateFactor(float value) {
		m_interpolate_factor = std::clamp<float>(value, 0.0f, 1.0f);
	}

	void MixedTextureMaterial::SetAttributes() {
		
		//ToDo : upload uniforms....
		if(!m_attribute_already_set) {
			assert(m_owner_mesh != nullptr);

			auto _interpolate_factor	=  std::any_cast<float>(m_uniform_collection["interpolate_factor"]);

			unsigned int index			= m_owner_mesh->GetIdentifier();
			auto uniform_array_name		= fmt::format("interpolate_factor_{0}", index);

			m_shader->SetUniform(uniform_array_name.c_str(), _interpolate_factor);

			m_attribute_already_set = true;
		}
	}
}
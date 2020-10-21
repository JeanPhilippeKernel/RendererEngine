#include "ShaderMaterial.h"
#include <algorithm>


namespace Z_Engine::Rendering::Materials {

	ShaderMaterial::ShaderMaterial(const Ref<Shaders::Shader>& shader, std::initializer_list<std::string>&& uniforms)
		:
		IMaterial(shader) 
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);
	}

	ShaderMaterial::ShaderMaterial(const char * shader_filename, std::initializer_list<std::string>&& uniforms)
		: 
		IMaterial() 
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);


		m_shader.reset(Shaders::CreateShader(shader_filename));
		m_shader->Bind();
	}

	ShaderMaterial::ShaderMaterial(std::initializer_list<std::string>&& uniforms)
		: 
		IMaterial() 
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);
	}

}
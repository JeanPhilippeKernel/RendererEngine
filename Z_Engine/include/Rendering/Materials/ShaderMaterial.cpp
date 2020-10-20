#include "ShaderMaterial.h"


namespace Z_Engine::Rendering::Materials {

	ShaderMaterial::ShaderMaterial(const Ref<Shaders::Shader>& shader, std::initializer_list<std::string>&& uniforms)
		:
		IMaterial(shader), 
		m_shader_manager(new Managers::ShaderManager()) 
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);
	}

	ShaderMaterial::ShaderMaterial(const char * shader_filename, std::initializer_list<std::string>&& uniforms)
		: 
		IMaterial(), 
		m_shader_manager(new Managers::ShaderManager())
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);
		m_shader =  m_shader_manager->Load(shader_filename);

	}

	ShaderMaterial::ShaderMaterial(std::initializer_list<std::string>&& uniforms)
		: 
		IMaterial(), 
		m_shader_manager(new Managers::ShaderManager())
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);
	}

}
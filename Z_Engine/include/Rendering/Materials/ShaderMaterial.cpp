#include "ShaderMaterial.h"
#include <algorithm>


namespace Z_Engine::Rendering::Materials {

	bool ShaderMaterial::m_default_uniform_initialized = false;

	ShaderMaterial::ShaderMaterial(const Ref<Shaders::Shader>& shader, std::initializer_list<std::string>&& uniforms)
		:
		IMaterial(shader), 
		m_shader_manager(new Managers::ShaderManager()),
		m_owner_mesh(nullptr),
		m_initialize_fn(nullptr),
		m_attribute_already_set(false)
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);

	}

	ShaderMaterial::ShaderMaterial(const char * shader_filename, std::initializer_list<std::string>&& uniforms)
		: 
		IMaterial(), 
		m_shader_manager(new Managers::ShaderManager()),
		m_owner_mesh(nullptr),
		m_initialize_fn(nullptr),
		m_attribute_already_set(false)
	{
		std::for_each(
			std::begin(uniforms), std::end(uniforms), 
			[this](const std::string& x) { m_uniform_collection.emplace(x, 0.0f); }
		);

		m_shader = m_shader_manager->Load(shader_filename);
		m_shader->Bind();
	}

	void ShaderMaterial::InitDefaultUniforms(const std::function<void()>& function) {
		assert(m_shader != nullptr);

		m_initialize_fn = function;
		if(m_initialize_fn != nullptr && !m_default_uniform_initialized){
			m_initialize_fn();
			m_default_uniform_initialized =  true;
		}
	}


	void ShaderMaterial::SetMeshOwner(Meshes::Mesh* const mesh) {
		m_owner_mesh =  mesh;
	}


}
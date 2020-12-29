#include "ShaderMaterial.h"
#include <algorithm>


namespace Z_Engine::Rendering::Materials {


	ShaderMaterial::ShaderMaterial(const Ref<Shaders::Shader>& shader)
		:
		IMaterial(shader), 
		m_shader_manager(new Managers::ShaderManager()),
		m_owner_mesh(nullptr)
	{
	}

	ShaderMaterial::ShaderMaterial(const char * shader_filename)
		: 
		IMaterial(), 
		m_shader_manager(new Managers::ShaderManager()),
		m_owner_mesh(nullptr)
	{
		m_shader = m_shader_manager->Load(shader_filename);
	}

	void ShaderMaterial::SetMeshOwner(Meshes::Mesh* const mesh) {
		m_owner_mesh =  mesh;
	}


}
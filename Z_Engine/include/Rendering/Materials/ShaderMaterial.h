#pragma once
#include <string>

#include "IMaterial.h"
#include "../../Managers/ShaderManager.h"

namespace Z_Engine::Rendering::Meshes {
	class Mesh;
}


namespace Z_Engine::Rendering::Materials {

	class ShaderMaterial : public IMaterial	{
	public:
		explicit ShaderMaterial(const Ref<Shaders::Shader>& shader);
		explicit ShaderMaterial(const char * shader_filename);

		virtual ~ShaderMaterial() = default;

		virtual void SetMeshOwner(Meshes::Mesh* const mesh);

		virtual void Apply() = 0;

	protected:
		Meshes::Mesh* m_owner_mesh;
		Ref<Managers::ShaderManager> m_shader_manager;
	};
}



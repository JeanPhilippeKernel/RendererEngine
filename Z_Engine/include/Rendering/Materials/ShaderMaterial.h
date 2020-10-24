#pragma once
#include <initializer_list>
#include <unordered_map>
#include <string>
#include <any>

#include <functional>

#include "IMaterial.h"
#include "../../Core/IInitializable.h"
#include "../../Managers/ShaderManager.h"

namespace Z_Engine::Rendering::Meshes {
	class Mesh;
}


namespace Z_Engine::Rendering::Materials {

	class ShaderMaterial : public IMaterial	
	{
	public:
		explicit ShaderMaterial(const Ref<Shaders::Shader>& shader, std::initializer_list<std::string>&& uniforms);
		explicit ShaderMaterial(const char * shader_filename, std::initializer_list<std::string>&& uniforms);

		virtual ~ShaderMaterial() =  default;

		std::any& operator[](const std::string& key) {
			return m_uniform_collection[key];
		} 

		virtual void SetMeshOwner(Meshes::Mesh* const mesh);
		virtual void SetAttributes() = 0;

	protected:
		bool m_attribute_already_set;
		static bool m_default_uniform_initialized;

		std::unordered_map<std::string, std::any> m_uniform_collection {};
		Meshes::Mesh* m_owner_mesh;
		Ref<Managers::ShaderManager> m_shader_manager;
	
		void InitDefaultUniforms(const std::function<void()>& function);

	private:
		std::function<void()> m_initialize_fn;
	};
}



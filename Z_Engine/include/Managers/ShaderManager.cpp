#include "ShaderManager.h"

namespace Z_Engine::Managers {

	ShaderManager::ShaderManager() : IAssetManager() {
		this->m_suffix = "_shader";
	}

	Ref<Rendering::Shaders::Shader>& ShaderManager::Add(const char* name, const char* filename) {
		const auto key = std::string(name).append(m_suffix);
		
		Ref<Rendering::Shaders::Shader> shader;
		shader.reset(Rendering::Shaders::CreateShader(filename));
		const auto result = IManager::Add(key, shader);
		
		assert(result.has_value() == true);
		return result->get();
	}

	Ref<Rendering::Shaders::Shader>& ShaderManager::Load(const char* filename) {
		std::filesystem::path p(filename);
		const auto name = p.stem();
		
		return Add(name.u8string().c_str(), filename);
	}
}




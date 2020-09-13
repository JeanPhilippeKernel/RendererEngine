#include "ShaderManager.h"

namespace Z_Engine::Managers {

	ShaderManager::ShaderManager() : IAssetManager() {
		this->m_suffix = "_shader";
	}

	void ShaderManager::Add(const char* name, const char* filename) {
		const auto key = std::string(name).append(m_suffix);
		
		Ref<Rendering::Shaders::Shader> shader;
		shader.reset(Rendering::Shaders::CreateShader(filename));
		IManager::Add(key, shader);
	}

	void ShaderManager::Load(const char* filename) {
		std::filesystem::path p(filename);
		const auto name = p.stem();
		Add(name.u8string().c_str(), filename);
	}
}




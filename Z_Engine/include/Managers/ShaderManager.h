#pragma once
#include "IAssetManager.h"
#include "../Rendering/Shaders/Shader.h"

#include <filesystem>


namespace Z_Engine::Managers {
	
	class ShaderManager : 
		public IAssetManager<Rendering::Shaders::Shader> 
	{
	public:
		ShaderManager();
		virtual ~ShaderManager() = default;

		Ref<Rendering::Shaders::Shader>& Add(const char* name, const char* filename) override;
		Ref<Rendering::Shaders::Shader>& Load(const char* filename) override;
	};
}

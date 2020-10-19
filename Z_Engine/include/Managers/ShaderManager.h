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
		virtual ~ShaderManager()	= default;

		void Add(const char* name, const char* filename) override;
		void Load(const char* filename) override;
	};
}

#pragma once
#include <unordered_map>

#include "IManager.h"
#include "../Rendering/Shaders/Shader.h"
#include "../Z_EngineDef.h"


#include <assert.h>


namespace Z_Engine::Managers {
	
	struct ShaderManager : 
		public IManager<std::string, Ref<Rendering::Shaders::Shader>> 
	{
		ShaderManager()						= delete;
		ShaderManager(const ShaderManager&) = delete;
		~ShaderManager()					= delete;

		static void Add(const char* name, const char* filename) {
			const auto key = std::string(name).append(m_suffix);
			const auto res = IManager::Exists(key);

			if (res.first) return;

			Z_Engine::Ref<Rendering::Shaders::Shader> shader;
			shader.reset(new Rendering::Shaders::Shader(filename));
			IManager::m_collection[key] = shader;
		}

		static void Load(const char* filename) {
			const auto path = std::string(filename);
			auto pos = path.find_last_of('/');
			auto rpos = path.rfind('.');

			auto name = path.substr(pos + 1, (rpos - pos) - 1);
			Add(name.c_str(), filename);
		}

		static Ref<Rendering::Shaders::Shader>& Get(const char* name) {
			const auto key = std::string(name).append(m_suffix);
			const auto result = IManager::Get(key);
			assert(result.has_value() == true);

			return result->get();
		}

	private:
		static std::string m_suffix;

	};
}

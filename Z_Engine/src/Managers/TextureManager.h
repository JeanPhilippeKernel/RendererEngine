#pragma once
#include <string>
#include <algorithm>
#include "IManager.h"
#include "../Rendering/Textures/Texture.h"
#include "../Z_EngineDef.h"

#include <assert.h>

namespace Z_Engine::Managers {
	
	struct TextureManager : 
		public IManager<std::string, Ref<Rendering::Textures::Texture>> 
	{
	public:
		TextureManager()						= delete;
		TextureManager(const TextureManager&)	= delete;
		~TextureManager()						= delete;


		static void Add(const char * name, const char * filename) {
			const auto key = std::string(name).append(m_suffix); 

			Z_Engine::Ref<Rendering::Textures::Texture> texture;
			texture.reset(Rendering::Textures::CreateTexture(filename));

			IManager::Add(key, texture);
		}
		
		static void Load(const char * filename) {
			const auto path = std::string(filename);
			auto pos = path.find_last_of('/');
			auto rpos =  path.rfind('.');
			
			auto name = path.substr(pos + 1, (rpos - pos) - 1);
			Add(name.c_str(), filename);
		}

		static Ref<Rendering::Textures::Texture>& Get(const char * name) {
			
			const auto key = std::string(name).append(m_suffix);
			const auto result  = IManager::Get(key);
			assert(result.has_value() == true);
			
			return result->get();
		}

	private:
		static std::string m_suffix;
	};

}

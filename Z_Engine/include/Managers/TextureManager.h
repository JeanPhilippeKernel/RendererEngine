#pragma once
#include "IAssetManager.h"
#include "../Rendering/Textures/Texture.h"
#include "../Z_EngineDef.h"

#include <assert.h>
#include <filesystem>


namespace Z_Engine::Managers {
	
	class TextureManager : 
		public IAssetManager<Rendering::Textures::Texture>
	{
	public:
		TextureManager();
		virtual ~TextureManager()	= default;

		Ref<Rendering::Textures::Texture>& Add(const char* name, const char* filename) override;
		Ref<Rendering::Textures::Texture>& Load(const char* filename) override;
		
		Ref<Rendering::Textures::Texture>& Add(const char* name, unsigned int width, unsigned int height);

	};

}

#pragma once
#include <Managers/IAssetManager.h>
#include <Rendering/Textures/Texture.h>
#include <ZEngineDef.h>

#include <assert.h>
#include <filesystem>


namespace ZEngine::Managers {
	
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

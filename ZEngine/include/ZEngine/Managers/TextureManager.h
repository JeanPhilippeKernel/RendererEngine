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

		/**
		* Add a texture to the Texture manager store
		*
		* @param name Name of the texture. This name must be unique in the entire store
		* @param filename Path to find the texture
		* @return A texture instance
		*/
		Ref<Rendering::Textures::Texture>& Add(const char* name, const char* filename) override;

		/**
		* Add a texture to the Texture manager store
		*
		* @param name Name of the texture. This name must be unique in the entire store
		* @param width Texture width
		* @param height Texture height
		* @return A texture instance
		*/
		Ref<Rendering::Textures::Texture>& Add(const char* name, unsigned int width, unsigned int height);

		/**
		* Add a texture to the Texture manager store
		*
		* @param filename Path to find the texture file in the system
		* @return A texture instance
		*/
		Ref<Rendering::Textures::Texture>& Load(const char* filename) override;
		

	};

}

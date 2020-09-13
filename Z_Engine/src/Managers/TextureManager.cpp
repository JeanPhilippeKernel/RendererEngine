#include "TextureManager.h"

namespace Z_Engine::Managers {

	TextureManager::TextureManager() : IAssetManager() {
		this->m_suffix = "_texture";
	}

	void TextureManager::Add(const char* name, const char* filename) {
		const auto key = std::string(name).append(m_suffix);

		Ref<Rendering::Textures::Texture> texture;
		texture.reset(Rendering::Textures::CreateTexture(filename));
		IManager::Add(key, texture);
	}

	void TextureManager::Add(const char* name, unsigned int width, unsigned int height) {
		const auto key = std::string(name).append(m_suffix);

		Z_Engine::Ref<Rendering::Textures::Texture> texture;
		texture.reset(Rendering::Textures::CreateTexture(width, height));
		IManager::Add(key, texture);
	}

	void TextureManager::Load(const char* filename) {
		std::filesystem::path p(filename);
		const auto name = p.stem();
		Add(name.u8string().c_str(), filename);
	}
}
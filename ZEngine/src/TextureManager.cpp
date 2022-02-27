#include <pch.h>
#include <Managers/TextureManager.h>

namespace ZEngine::Managers {

    TextureManager::TextureManager() : IAssetManager() {
        this->m_suffix = "_texture";
    }

    Ref<Rendering::Textures::Texture>& TextureManager::Add(const char* name, const char* filename) {
        const auto key = std::string(name).append(m_suffix);

        const auto found = IManager::Exists(key);
        if (found.first) {
            return found.second->second;
        }

        Ref<Rendering::Textures::Texture> texture;
        texture.reset(Rendering::Textures::CreateTexture(filename));
        auto result = IManager::Add(key, texture);

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Textures::Texture>& TextureManager::Add(const char* name, unsigned int width, unsigned int height) {
        const auto key = std::string(name).append(m_suffix);

        const auto found = IManager::Exists(key);
        if (found.first) {
            return found.second->second;
        }

        ZEngine::Ref<Rendering::Textures::Texture> texture;
        texture.reset(Rendering::Textures::CreateTexture(width, height));
        auto result = IManager::Add(key, texture);

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Textures::Texture>& TextureManager::Load(const char* filename) {
        std::filesystem::path p(filename);
        const auto            name = p.stem();
        return Add(reinterpret_cast<const char*>(name.u8string().c_str()), filename);
    }
} // namespace ZEngine::Managers

#include <pch.h>
#include <Core/Coroutine.h>
#include <Managers/TextureManager.h>

namespace ZEngine::Managers
{

    CoreTextureManager::CoreTextureManager() : IAssetManager()
    {
        this->m_suffix = "_texture";
    }

    Ref<Rendering::Textures::Texture> CoreTextureManager::Add(const char* name, const char* filename)
    {
        const auto key = std::string(name).append(m_suffix);

        const auto found = IManager::Exists(key);
        if (found.first)
        {
            return found.second->second;
        }

        Ref<Rendering::Textures::Texture> texture;
        texture.reset(Rendering::Textures::CreateTexture(filename));
        auto result = IManager::Add(key, texture);

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Textures::Texture> CoreTextureManager::Add(const char* name, unsigned int width, unsigned int height)
    {
        const auto key = std::string(name).append(m_suffix);

        const auto found = IManager::Exists(key);
        if (found.first)
        {
            return found.second->second;
        }

        ZEngine::Ref<Rendering::Textures::Texture> texture;
        texture.reset(Rendering::Textures::CreateTexture(width, height));
        auto result = IManager::Add(key, texture);

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Textures::Texture> CoreTextureManager::Load(const char* filename)
    {
        std::filesystem::path p(filename);
        const auto            name = p.stem();
        return Add(reinterpret_cast<const char*>(name.u8string().c_str()), filename);
    }

    ZEngine::Scope<CoreTextureManager> TextureManager::m_texture_manager = CreateScope<CoreTextureManager>();

    Ref<Rendering::Textures::Texture> TextureManager::Add(std::string_view name, std::string_view filename)
    {
        auto texture = m_texture_manager->Add(name.data(), filename.data());
        return texture;
    }

    Ref<Rendering::Textures::Texture> TextureManager::Add(std::string_view name, unsigned int width, unsigned int height)
    {
        auto texture = m_texture_manager->Add(name.data(), width, height);
        return texture;
    }

    Ref<Rendering::Textures::Texture> TextureManager::Load(std::string_view filename)
    {
        auto texture = m_texture_manager->Load(filename.data());
        return texture;
    }
} // namespace ZEngine::Managers

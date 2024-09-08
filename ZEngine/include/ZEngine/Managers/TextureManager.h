#pragma once
#include <Managers/IAssetManager.h>
#include <Rendering/Textures/Texture.h>
#include <ZEngineDef.h>
#include <assert.h>
#include <filesystem>
#include <future>

namespace ZEngine::Managers
{

    class CoreTextureManager : public IAssetManager<Rendering::Textures::Texture>
    {
    public:
        CoreTextureManager();
        virtual ~CoreTextureManager() = default;

        /**
         * Add a texture to the Texture manager store
         *
         * @param name Name of the texture. This name must be unique in the entire store
         * @param filename Path to find the texture
         * @return A texture instance
         */
        Ref<Rendering::Textures::Texture> Add(const char* name, const char* filename) override;

        /**
         * Add a texture to the Texture manager store
         *
         * @param name Name of the texture. This name must be unique in the entire store
         * @param width Texture width
         * @param height Texture height
         * @return A texture instance
         */
        Ref<Rendering::Textures::Texture> Add(const char* name, unsigned int width, unsigned int height);

        /**
         * Add a texture to the Texture manager store
         *
         * @param filename Path to find the texture file in the system
         * @return A texture instance
         */
        Ref<Rendering::Textures::Texture> Load(const char* filename) override;
    };

    struct TextureManager
    {
    public:
        TextureManager()                                 = delete;
        TextureManager(const TextureManager&)            = delete;
        TextureManager(TextureManager&&)                 = delete;
        TextureManager& operator=(const TextureManager&) = delete;
        ~TextureManager()                                = default;

        /**
         * Add a texture to the Texture manager store
         *
         * @param name Name of the texture. This name must be unique in the entire store
         * @param filename Path to find the texture
         * @return A texture instance
         */
        static Ref<Rendering::Textures::Texture> Add(std::string_view name, std::string_view filename);

        /**
         * Add a texture to the Texture manager store
         *
         * @param name Name of the texture. This name must be unique in the entire store
         * @param width Texture width
         * @param height Texture height
         * @return A texture instance
         */
        static Ref<Rendering::Textures::Texture> Add(std::string_view name, unsigned int width, unsigned int height);

        /**
         * Add a texture to the Texture manager store
         *
         * @param filename Path to find the texture file in the system
         * @return A texture instance
         */
        static Ref<Rendering::Textures::Texture> Load(std::string_view filename);

    private:
        static ZEngine::Scope<ZEngine::Managers::CoreTextureManager> m_texture_manager;
    };

} // namespace ZEngine::Managers

#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Textures
{
    Texture::~Texture()
    {
        Dispose();
    }

    void Texture::Dispose() {}
} // namespace ZEngine::Rendering::Textures

#include <Hardwares/VulkanDevice.h>
#include <Texture.h>

namespace ZEngine::Rendering::Textures
{
    Texture::~Texture()
    {
        Dispose();
    }

    void Texture::Dispose() {}
} // namespace ZEngine::Rendering::Textures

#pragma once
#include <ZEngine/Rendering/Textures/Texture.h>
#include <cstdint>

namespace ZEngine::Rendering
{
    enum class SceneTextureDescriptorState : uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    /// Render-owner service for the device-owned, immutable bindless target set.
    /// A publication identifies exact image/view contents, not queue emptiness.
    /// The caller must retain the image version through every CPU/GPU consumer.
    struct ISceneTextureDescriptorPreparation
    {
        virtual ~ISceneTextureDescriptorPreparation()                                                                                                                             = default;
        virtual SceneTextureDescriptorState PrepareSceneTextureDescriptor(Textures::TextureHandle texture, Helpers::Handle<Hardwares::ImageBuffer> image, uint64_t* publication)  = 0;
        virtual bool                        IsSceneTextureDescriptorCurrent(Textures::TextureHandle texture, Helpers::Handle<Hardwares::ImageBuffer> image, uint64_t publication) = 0;
    };
} // namespace ZEngine::Rendering

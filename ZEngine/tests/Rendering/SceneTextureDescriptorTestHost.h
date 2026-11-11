#pragma once
#include <ZEngine/Rendering/SceneTextureDescriptorPreparation.h>

namespace ZEngine::Tests
{
    struct SceneTextureDescriptorTestHost : Rendering::ISceneTextureDescriptorPreparation
    {
        Rendering::SceneTextureDescriptorState State   = Rendering::SceneTextureDescriptorState::Ready;
        uint64_t                               Version = 1;
        uint32_t                               Calls   = 0;
        bool                                   Current = true;
        struct Binding
        {
            Rendering::Textures::TextureHandle      Texture = {};
            Helpers::Handle<Hardwares::ImageBuffer> Image   = {};
        };
        Binding                                Bindings[8] = {};

        Rendering::SceneTextureDescriptorState PrepareSceneTextureDescriptor(Rendering::Textures::TextureHandle texture, Helpers::Handle<Hardwares::ImageBuffer> image, uint64_t* publication) override
        {
            ++Calls;
            if (texture.Index >= 8 || !publication)
            {
                return Rendering::SceneTextureDescriptorState::Failed;
            }
            if (State == Rendering::SceneTextureDescriptorState::Ready)
            {
                Bindings[texture.Index] = {texture, image};
                *publication            = Version;
            }
            return State;
        }

        bool IsSceneTextureDescriptorCurrent(Rendering::Textures::TextureHandle texture, Helpers::Handle<Hardwares::ImageBuffer> image, uint64_t publication) override
        {
            if (!Current || !publication || publication != Version || texture.Index >= 8)
            {
                return false;
            }
            const auto& binding = Bindings[texture.Index];
            return binding.Texture.Generation == texture.Generation && binding.Image.Index == image.Index && binding.Image.Generation == image.Generation;
        }
    };
} // namespace ZEngine::Tests

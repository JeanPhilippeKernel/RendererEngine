#pragma once
#include <ZEngine/Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials
{

    class BasicMaterial : public ShaderMaterial
    {
    public:
        explicit BasicMaterial();
        virtual ~BasicMaterial() = default;

        void               SetTexture(const Textures::Texture*);
        Textures::Texture* GetTexture() const;

        void               Apply(const Helpers::Ref<Shaders::Shader>&) override;

    private:
        Textures::Texture* m_texture;
    };
} // namespace ZEngine::Rendering::Materials

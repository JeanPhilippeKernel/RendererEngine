#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials
{

    class BasicMaterial : public ShaderMaterial
    {
    public:
        explicit BasicMaterial();
        virtual ~BasicMaterial() = default;

        void                            SetTexture(const Helpers::Ref<Textures::Texture>&);
        Helpers::Ref<Textures::Texture> GetTexture() const;

        void Apply(const Helpers::Ref<Shaders::Shader>&) override;

    private:
        Helpers::Ref<Textures::Texture> m_texture;
    };
} // namespace ZEngine::Rendering::Materials

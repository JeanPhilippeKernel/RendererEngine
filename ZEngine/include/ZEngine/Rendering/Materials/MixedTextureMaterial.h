#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    class MixedTextureMaterial : public ShaderMaterial {
    public:
        explicit MixedTextureMaterial();
        virtual ~MixedTextureMaterial() = default;

        unsigned int GetHashCode() override;

        void Apply() override;

        void SetSecondTexture(Ref<Textures::Texture>& texture);
        void SetSecondTexture(Textures::Texture* const texture);

        void SetInterpolateFactor(float value);

    private:
        float                  m_interpolate_factor;
        Ref<Textures::Texture> m_second_texture;
    };
} // namespace ZEngine::Rendering::Materials

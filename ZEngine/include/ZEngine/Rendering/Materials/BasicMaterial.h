#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    class BasicMaterial : public ShaderMaterial {
    public:
        explicit BasicMaterial();
        virtual ~BasicMaterial() = default;

        void                   SetTexture(const Ref<Textures::Texture>&);
        Ref<Textures::Texture> GetTexture() const;

        void Apply(Shaders::Shader* const shader) override;

    private:
        Ref<Textures::Texture> m_texture;
    };
} // namespace ZEngine::Rendering::Materials

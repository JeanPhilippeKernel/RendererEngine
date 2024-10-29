#pragma once
#include <Maths/Math.h>
#include <Rendering/Lights/Light.h>
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials
{

    class StandardMaterial : public ShaderMaterial
    {
    public:
        explicit StandardMaterial();
        virtual ~StandardMaterial() = default;

        void SetTileFactor(float value);
        void SetDiffuseTintColor(const glm::vec4& value);
        void SetSpecularTintColor(const glm::vec4& value);

        void SetShininess(float value);

        void Apply(const Helpers::Ref<Shaders::Shader>&) override;

        void SetSpecularMap(const Helpers::Ref<Textures::Texture>& texture);
        void SetDiffuseMap(const Helpers::Ref<Textures::Texture>& texture);

        float                 GetTileFactor() const;
        float                 GetShininess() const;
        const Maths::Vector4& GetDiffuseTintColor() const;
        const Maths::Vector4& GetSpecularTintColor() const;

        Helpers::Ref<Textures::Texture> GetSpecularMap() const;
        Helpers::Ref<Textures::Texture> GetDiffuseMap() const;

    private:
        float                           m_shininess;
        float                           m_tile_factor;
        Maths::Vector4                  m_diffuse_tint_color;
        Maths::Vector4                  m_specular_tint_color;
        Helpers::Ref<Textures::Texture> m_diffuse_map;
        Helpers::Ref<Textures::Texture> m_specular_map;
    };
} // namespace ZEngine::Rendering::Materials

#pragma once
#include <Core/Maths/Vec.h>
#include <Rendering/Lights/Light.h>
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials
{

    class StandardMaterial : public ShaderMaterial
    {
    public:
        explicit StandardMaterial();
        virtual ~StandardMaterial() = default;

        void                               SetTileFactor(float value);
        void                               SetDiffuseTintColor(const ZEngine::Core::Maths::Vec4f& value);
        void                               SetSpecularTintColor(const ZEngine::Core::Maths::Vec4f& value);

        void                               SetShininess(float value);

        void                               Apply(const Helpers::Ref<Shaders::Shader>&) override;

        void                               SetSpecularMap(const Textures::Texture* texture);
        void                               SetDiffuseMap(const Textures::Texture* texture);

        float                              GetTileFactor() const;
        float                              GetShininess() const;
        const ZEngine::Core::Maths::Vec4f& GetDiffuseTintColor() const;
        const ZEngine::Core::Maths::Vec4f& GetSpecularTintColor() const;

        Textures::Texture*                 GetSpecularMap() const;
        Textures::Texture*                 GetDiffuseMap() const;

    private:
        float                       m_shininess;
        float                       m_tile_factor;
        ZEngine::Core::Maths::Vec4f m_diffuse_tint_color;
        ZEngine::Core::Maths::Vec4f m_specular_tint_color;
        Textures::Texture*          m_diffuse_map;
        Textures::Texture*          m_specular_map;
    };
} // namespace ZEngine::Rendering::Materials

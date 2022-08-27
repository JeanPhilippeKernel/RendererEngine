#pragma once
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Materials {

    class StandardMaterial : public ShaderMaterial {
    public:
        explicit StandardMaterial();
        virtual ~StandardMaterial() = default;

        void SetTileFactor(float value);
        void SetTintColor(const glm::vec4& value);

        void SetShininess(float value);

        void SetLight(const Ref<Lights::BasicLight>& light);
        bool HasLight() const;

        void Apply(const Ref<Shaders::Shader>&) override;

        void SetSpecularMap(const Ref<Textures::Texture>& texture);
        void SetDiffuseMap(const Ref<Textures::Texture>& texture);

        float                 GetTileFactor() const;
        float                 GetShininess() const;
        const Maths::Vector4& GetTintColor() const;

        Ref<Textures::Texture> GetSpecularMap() const;
        Ref<Textures::Texture> GetDiffuseMap() const;

    private:
        float                  m_shininess;
        float                  m_tile_factor;
        glm::vec4              m_tint_color;
        Ref<Textures::Texture> m_diffuse_map;
        Ref<Textures::Texture> m_specular_map;

    private:
        WeakRef<Lights::BasicLight> m_light;
    };
} // namespace ZEngine::Rendering::Materials

#pragma once
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Materials {

    class StandardMaterial : public ShaderMaterial {
    public:
        explicit StandardMaterial();
        virtual ~StandardMaterial() = default;

        unsigned int GetHashCode() override;

        void SetTileFactor(float value);
        void SetTintColor(const glm::vec4& value);

        void SetShininess(float value);

        void SetLight(const Ref<Lights::BasicLight>& light);
        void SetViewPosition(const glm::vec3& position);

        void Apply(Shaders::Shader* const shader) override;

        void SetSpecularMap(const Ref<Textures::Texture>& texture);
        void SetSpecularMap(Textures::Texture* const texture);

        void SetDiffuseMap(const Ref<Textures::Texture>& texture);
        void SetDiffuseMap(Textures::Texture* const texture);

        void SetTexture(Textures::Texture* const texture) override;
        void SetTexture(const Ref<Textures::Texture>& texture) override;

        float                 GetTileFactor() const;
        float                 GetShininess() const;
        const Maths::Vector4& GetTintColor() const;

        Ref<Textures::Texture> GetSpecularMap() const;
        Ref<Textures::Texture> GetDiffuseMap() const;

    private:
        float                  m_shininess;
        float                  m_tile_factor;
        glm::vec4              m_tint_color;
        Ref<Textures::Texture> m_specular_map;

    private:
        glm::vec3                   m_view_position;
        WeakRef<Lights::BasicLight> m_light;
    };
} // namespace ZEngine::Rendering::Materials

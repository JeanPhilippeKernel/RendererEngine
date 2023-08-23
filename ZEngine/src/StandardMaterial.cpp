#include <pch.h>
#include <Rendering/Materials/StandardMaterial.h>

namespace ZEngine::Rendering::Materials {

    StandardMaterial::StandardMaterial()
        : ShaderMaterial(Shaders::ShaderBuiltInType::STANDARD),
          m_tile_factor(1.0f),
          m_diffuse_tint_color(glm::vec4(1.0f)),
          m_specular_tint_color(glm::vec4(1.0f)),
          m_diffuse_map(Textures::CreateTexture(1, 1)),
          m_specular_map(Textures::CreateTexture(1, 1)) {
        m_material_name = typeid(*this).name();
    }

    void StandardMaterial::SetTileFactor(float value) {
        m_tile_factor = value;
    }

    void StandardMaterial::SetDiffuseTintColor(const glm::vec4& value) {
        m_diffuse_tint_color = value;
    }

    void StandardMaterial::SetSpecularTintColor(const glm::vec4& value) {
        m_specular_tint_color = value;
    }

    void StandardMaterial::SetShininess(float value) {
        m_shininess = value;
    }

    void StandardMaterial::SetSpecularMap(const Ref<Textures::Texture>& texture) {
        m_specular_map = texture;
    }

    void StandardMaterial::SetDiffuseMap(const Ref<Textures::Texture>& texture) {
        m_diffuse_map = texture;
    }

    float StandardMaterial::GetTileFactor() const {
        return m_tile_factor;
    }

    float StandardMaterial::GetShininess() const {
        return m_shininess;
    }

    const Maths::Vector4& StandardMaterial::GetDiffuseTintColor() const {
        return m_diffuse_tint_color;
    }

    const Maths::Vector4& StandardMaterial::GetSpecularTintColor() const {
        return m_specular_tint_color;
    }

    Ref<Textures::Texture> StandardMaterial::GetSpecularMap() const {
        return m_specular_map;
    }

    Ref<Textures::Texture> StandardMaterial::GetDiffuseMap() const {
        return m_diffuse_map;
    }

    void StandardMaterial::Apply(const Ref<Shaders::Shader>& shader) {
        //ShaderMaterial::Apply(shader);

        //shader->SetUniform("Material.TilingFactor", m_tile_factor);
        //shader->SetUniform("Material.Shininess", m_shininess);
        //shader->SetUniform("Material.DiffuseTintColor", m_diffuse_tint_color);
        //shader->SetUniform("Material.SpecularTintColor", m_specular_tint_color);

        //shader->SetUniform("Material.Diffuse", 0);
        //shader->SetUniform("Material.Specular", 1);

        //m_diffuse_map->Bind();
        //m_specular_map->Bind(1);
    }
} // namespace ZEngine::Rendering::Materials

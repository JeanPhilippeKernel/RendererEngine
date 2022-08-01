#include <pch.h>
#include <Rendering/Materials/StandardMaterial.h>

namespace ZEngine::Rendering::Materials {

    StandardMaterial::StandardMaterial()
        : ShaderMaterial(Shaders::ShaderBuiltInType::STANDARD), m_tile_factor(1.0f), m_tint_color(glm::vec4(1.0f)), m_specular_map(Textures::CreateTexture(1, 1)) {
        m_material_name = typeid(*this).name();
    }

    unsigned int StandardMaterial::GetHashCode() {
        auto hash = static_cast<unsigned int>(m_tile_factor) ^ static_cast<unsigned int>(m_tint_color.x) ^ static_cast<unsigned int>(m_tint_color.y)
                  ^ static_cast<unsigned int>(m_tint_color.z);

        return hash ^ ShaderMaterial::GetHashCode();
    }

    void StandardMaterial::SetTileFactor(float value) {
        m_tile_factor = value;
    }

    void StandardMaterial::SetTintColor(const glm::vec4& value) {
        m_tint_color = value;
    }

    void StandardMaterial::SetShininess(float value) {
        m_shininess = value;
    }

    void StandardMaterial::SetLight(const Ref<Lights::BasicLight>& light) {
        m_light = light;
    }

    void StandardMaterial::SetViewPosition(const glm::vec3& position) {
        m_view_position = position;
    }

    void StandardMaterial::SetSpecularMap(const Ref<Textures::Texture>& texture) {
        m_specular_map = texture;
    }

    void StandardMaterial::SetSpecularMap(Textures::Texture* const texture) {
        m_specular_map.reset(texture);
    }

    void StandardMaterial::SetDiffuseMap(const Ref<Textures::Texture>& texture) {
        m_texture = texture;
    }

    void StandardMaterial::SetDiffuseMap(Textures::Texture* const texture) {
        m_texture.reset(texture);
    }

    void StandardMaterial::SetTexture(const Ref<Textures::Texture>& texture) {
        SetDiffuseMap(texture);
    }

    void StandardMaterial::SetTexture(Textures::Texture* const texture) {
        SetDiffuseMap(texture);
    }

    void StandardMaterial::Apply(Shaders::Shader* const shader) {
        ShaderMaterial::Apply(shader);

        shader->SetUniform("material.tiling_factor", m_tile_factor);
        shader->SetUniform("material.tint_color", m_tint_color);

        shader->SetUniform("material.diffuse", 0);
        shader->SetUniform("material.specular", 1);
        shader->SetUniform("material.shininess", m_shininess);

        shader->SetUniform("view_position", m_view_position);

        if (!m_light.expired()) {
            const auto light = m_light.lock();

            shader->SetUniform("light.position", light->GetPosition());
            shader->SetUniform("light.ambient", light->GetAmbientColor());
            shader->SetUniform("light.diffuse", light->GetDiffuseColor());
            shader->SetUniform("light.specular", light->GetSpecularColor());
        }

        m_texture->Bind();
        m_specular_map->Bind(1);
    }
} // namespace ZEngine::Rendering::Materials

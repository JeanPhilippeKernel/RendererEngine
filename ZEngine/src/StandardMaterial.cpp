#include <pch.h>
#include <Rendering/Materials/StandardMaterial.h>

namespace ZEngine::Rendering::Materials {

    StandardMaterial::StandardMaterial()
        :
#ifdef _WIN32
          ShaderMaterial("Resources/Windows/Shaders/standard_shader.glsl"),
#else
          ShaderMaterial("Resources/Unix/Shaders/standard_shader.glsl"),
#endif
          m_tile_factor(1.0f),
          m_ambiant_light_strength(1.0f),
          m_tint_color(glm::vec4(1.0f)),
          m_light_source_color(glm::vec3(1.0f)) {
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

    void StandardMaterial::SetAmbiantLightStrength(float value) {
        m_ambiant_light_strength = value;
    }

    void StandardMaterial::SetLightSourceColor(const glm::vec3& value) {
        m_light_source_color = value;
    }

    void StandardMaterial::Apply() {
        ShaderMaterial::Apply();

        m_shader->SetUniform("material.tiling_factor", m_tile_factor);
        m_shader->SetUniform("material.tint_color", m_tint_color);

        m_shader->SetUniform("light.ambient_strength", m_ambiant_light_strength);
        m_shader->SetUniform("light.source_color", m_light_source_color);

        m_texture->Bind();
    }
} // namespace ZEngine::Rendering::Materials

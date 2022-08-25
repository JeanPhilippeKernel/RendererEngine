#include <pch.h>
#include <Rendering/Materials/MixedTextureMaterial.h>

namespace ZEngine::Rendering::Materials {

    MixedTextureMaterial::MixedTextureMaterial() : ShaderMaterial(Shaders::ShaderBuiltInType::MIXED_TEXTURE) {
        m_material_name = typeid(*(this)).name();
    }

    unsigned int MixedTextureMaterial::GetHashCode() {
        auto hash = static_cast<unsigned int>(m_interpolate_factor) ^ m_second_texture->GetIdentifier();

        return hash ^ ShaderMaterial::GetHashCode();
    }

    void MixedTextureMaterial::SetSecondTexture(Ref<Textures::Texture>& texture) {
        m_second_texture = texture;
    }

    void MixedTextureMaterial::SetSecondTexture(Textures::Texture* const texture) {
        m_second_texture.reset(texture);
    }

    void MixedTextureMaterial::SetInterpolateFactor(float value) {
        m_interpolate_factor = std::clamp<float>(value, 0.0f, 1.0f);
    }

    void MixedTextureMaterial::Apply(Shaders::Shader* const shader) {
        ShaderMaterial::Apply(shader);

        shader->SetUniform("texture_sampler_0", 0);
        shader->SetUniform("texture_sampler_1", 1);
        shader->SetUniform("material.interpolation_factor", m_interpolate_factor);

        //m_texture->Bind();
        m_second_texture->Bind(1);
    }

} // namespace ZEngine::Rendering::Materials

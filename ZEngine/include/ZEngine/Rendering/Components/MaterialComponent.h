#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Components {
    struct MaterialComponent {
        MaterialComponent(const Ref<Materials::ShaderMaterial>& shader_material) {
            m_shader_material = shader_material;
        }

        MaterialComponent(Ref<Materials::ShaderMaterial>&& shader_material) {
            m_shader_material = shader_material;
        }

        MaterialComponent(Materials::ShaderMaterial* const shader_material) : m_shader_material(shader_material) {}

        Ref<Materials::ShaderMaterial> GetMaterial() const {
            return m_shader_material;
        }

    private:
        Ref<Materials::ShaderMaterial> m_shader_material;
    };

} // namespace ZEngine::Rendering::Components

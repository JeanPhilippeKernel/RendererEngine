#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Components {
    struct MaterialComponent {
        MaterialComponent(const Ref<Materials::ShaderMaterial>& shader_material) {
            m_shader_material_collection.push_back(shader_material);
        }

        MaterialComponent(Ref<Materials::ShaderMaterial>&& shader_material) {
            m_shader_material_collection.push_back(std::move(shader_material));
        }

        MaterialComponent(const std::vector<Ref<Materials::ShaderMaterial>>& shader_materials) {
            m_shader_material_collection = shader_materials;
        }

        MaterialComponent(std::vector<Ref<Materials::ShaderMaterial>>&& shader_materials) {
            m_shader_material_collection = std::move(shader_materials);
        }

        void AddMaterial(const Ref<Materials::ShaderMaterial>& material) {
            m_shader_material_collection.push_back(material);
        }

        void AddMaterial(Ref<Materials::ShaderMaterial>&& material) {
            m_shader_material_collection.push_back(std::move(material));
        }

        const std::vector<Ref<Materials::ShaderMaterial>>& GetMaterials() const {
            return m_shader_material_collection;
        }

    private:
        std::vector<Ref<Materials::ShaderMaterial>> m_shader_material_collection;
    };

} // namespace ZEngine::Rendering::Components

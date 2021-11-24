#include <pch.h>
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    ShaderMaterial::ShaderMaterial(const Ref<Shaders::Shader>& shader) : IMaterial(shader), m_shader_manager(new Managers::ShaderManager()) {}

    ShaderMaterial::ShaderMaterial(const char* shader_filename) : IMaterial(), m_shader_manager(new Managers::ShaderManager()) {
        m_shader = m_shader_manager->Load(shader_filename);
    }
} // namespace ZEngine::Rendering::Materials

#include <pch.h>
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    ShaderMaterial::ShaderMaterial(Shaders::ShaderBuiltInType type) : IMaterial() {
        m_shader_built_in_type = type;
    }

    void ShaderMaterial::Apply(Shaders::Shader* const shader) {
        assert(shader != nullptr);

        shader->Bind();
    }
} // namespace ZEngine::Rendering::Materials

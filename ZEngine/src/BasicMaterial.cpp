#include <pch.h>
#include <Rendering/Materials/BasicMaterial.h>

namespace ZEngine::Rendering::Materials {

    BasicMaterial::BasicMaterial()
        :
#ifdef _WIN32
          ShaderMaterial("Resources/Windows/Shaders/basic_shader.glsl")
#else
          ShaderMaterial("Resources/Unix/Shaders/basic_shader.glsl")
#endif
    {
        m_material_name = typeid(*this).name();
        m_texture.reset(Textures::CreateTexture(1, 1));
    }

    void BasicMaterial::Apply() {
        ShaderMaterial::Apply();
        m_texture->Bind();
    }
} // namespace ZEngine::Rendering::Materials

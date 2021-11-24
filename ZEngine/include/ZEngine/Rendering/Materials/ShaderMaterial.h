#pragma once
#include <string>
#include <Rendering/Materials/IMaterial.h>
#include <Managers/ShaderManager.h>

namespace ZEngine::Rendering::Materials {

    class ShaderMaterial : public IMaterial {
    public:
        explicit ShaderMaterial(const Ref<Shaders::Shader>& shader);
        explicit ShaderMaterial(const char* shader_filename);

        virtual ~ShaderMaterial() = default;

        virtual void Apply() = 0;

    protected:
        Ref<Managers::ShaderManager> m_shader_manager;
    };
} // namespace ZEngine::Rendering::Materials

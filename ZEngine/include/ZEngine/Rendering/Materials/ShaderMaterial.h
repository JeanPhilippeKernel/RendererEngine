#pragma once
#include <string>
#include <Rendering/Materials/IMaterial.h>
#include <Rendering/Shaders/Shader.h>

namespace ZEngine::Rendering::Materials {

    class ShaderMaterial : public IMaterial {
    public:
        explicit ShaderMaterial(Shaders::ShaderBuiltInType type);

        virtual ~ShaderMaterial() = default;

        virtual void Apply(Shaders::Shader* const);
    };
} // namespace ZEngine::Rendering::Materials

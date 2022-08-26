#pragma once
#include <Rendering/Materials/IMaterial.h>
#include <Rendering/Shaders/Shader.h>

namespace ZEngine::Rendering::Materials {

    class ShaderMaterial : public IMaterial {
    public:
        explicit ShaderMaterial(Shaders::ShaderBuiltInType type);

        virtual ~ShaderMaterial() = default;

        virtual void Apply(const Ref<Shaders::Shader>&);
    };
} // namespace ZEngine::Rendering::Materials

#pragma once
#include <ZEngine/Rendering/Materials/IMaterial.h>
#include <ZEngine/Rendering/Shaders/Shader.h>

namespace ZEngine::Rendering::Materials
{

    class ShaderMaterial : public IMaterial
    {
    public:
        explicit ShaderMaterial(Shaders::ShaderBuiltInType type);

        virtual ~ShaderMaterial() = default;

        virtual void Apply(const Helpers::Ref<Shaders::Shader>&);
    };
} // namespace ZEngine::Rendering::Materials

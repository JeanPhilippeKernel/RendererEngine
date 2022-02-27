#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    class BasicMaterial : public ShaderMaterial {
    public:
        explicit BasicMaterial();
        virtual ~BasicMaterial() = default;

        void Apply() override;
    };
} // namespace ZEngine::Rendering::Materials

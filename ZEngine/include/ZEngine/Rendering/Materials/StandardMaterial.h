#pragma once
#include <Rendering/Materials/ShaderMaterial.h>

namespace ZEngine::Rendering::Materials {

    class StandardMaterial : public ShaderMaterial {
    public:
        explicit StandardMaterial();
        virtual ~StandardMaterial() = default;

        unsigned int GetHashCode() override;

        void SetTileFactor(float value);
        void SetTintColor(const glm::vec4& value);

        void SetAmbiantLightStrength(float value);
        void SetLightSourceColor(const glm::vec3& value);

        void Apply() override;

    private:
        float     m_tile_factor;
        float     m_ambiant_light_strength;
        glm::vec4 m_tint_color;
        glm::vec3 m_light_source_color;
    };
} // namespace ZEngine::Rendering::Materials

#pragma once
#include <string>
#include <typeinfo>
#include <Rendering/Textures/Texture.h>
#include <ZEngineDef.h>

#include <Rendering/Shaders/ShaderEnums.h>

namespace ZEngine::Rendering::Materials {

    struct IMaterial {
    public:
        explicit IMaterial() {
            m_material_name = typeid(*this).name();
        }

        explicit IMaterial(const Ref<Textures::Texture>& texture) {
            m_material_name = typeid(*this).name();
        }

        explicit IMaterial(Ref<Textures::Texture>&& texture) {
            m_material_name = typeid(*this).name();
        }

        virtual ~IMaterial() = default;

        virtual void SetShaderBuiltInType(Shaders::ShaderBuiltInType type) {
            m_shader_built_in_type = type;
        }

        virtual const std::string& GetName() const {
            return m_material_name;
        }

        virtual Shaders::ShaderBuiltInType GetShaderBuiltInType() const {
            return m_shader_built_in_type;
        }


    protected:
        std::string                m_material_name;
        Shaders::ShaderBuiltInType m_shader_built_in_type;
    };
} // namespace ZEngine::Rendering::Materials

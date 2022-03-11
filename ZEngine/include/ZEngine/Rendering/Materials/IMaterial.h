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

        explicit IMaterial(const Ref<Textures::Texture>& texture) : m_texture(texture) {
            m_material_name = typeid(*this).name();
        }


        explicit IMaterial(Ref<Textures::Texture>&& texture) : m_texture(texture) {
            m_material_name = typeid(*this).name();
        }

        virtual ~IMaterial() = default;

        virtual void SetTexture(const Ref<Textures::Texture>& texture) {
            m_texture = texture;
        }

        virtual void SetTexture(Textures::Texture* const texture) {
            m_texture.reset(texture);
        }

        virtual void SetShaderBuiltInType(Shaders::ShaderBuiltInType type) {
            m_shader_built_in_type = type;
        }

        virtual const std::string& GetName() const {
            return m_material_name;
        }

        virtual const Ref<Textures::Texture>& GetTexture() const {
            return m_texture;
        }

        virtual Shaders::ShaderBuiltInType GetShaderBuiltInType() const {
            return m_shader_built_in_type;
        }

        // THIS PART NEED TO BE MOVED TO AN INTERFACE....
        virtual unsigned int GetHashCode() {
            auto texture_id = m_texture->GetIdentifier();
            return static_cast<unsigned int>(texture_id ^ (int) m_shader_built_in_type);
        }

    protected:
        std::string                m_material_name;
        Shaders::ShaderBuiltInType m_shader_built_in_type;
        Ref<Textures::Texture>     m_texture{nullptr};
    };
} // namespace ZEngine::Rendering::Materials

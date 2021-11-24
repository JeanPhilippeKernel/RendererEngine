#pragma once
#include <string>
#include <Rendering/Shaders/ShaderEnums.h>
#include <glad/glad.h>

namespace ZEngine::Rendering::Shaders {

    struct ShaderInformation {
        /**
         * Shader identifier
         */
        GLuint ShaderId;
        /**
         * Shader program identifier
         */
        GLuint ProgramId;
        /**
         * Enumeration of shader
         * @see https://docs.gl/gl4/glCreateShader
         */
        GLenum InternalType;
        /**
         * Wether the shader has been compiled once
         */
        bool CompiledOnce;
        /**
         * Graphic Shader type
         * @see ShaderType
         */
        ShaderType Type;
        /**
         * Name of shader
         */
        std::string Name;
        /**
         * Source of shader
         */
        std::string Source;
    };
} // namespace ZEngine::Rendering::Shaders

#pragma once

namespace ZEngine::Rendering::Shaders {

    enum class ShaderType {
        /**
         * Vertex shader type
         */
        VERTEX = 0,
        /**
         * Fragment shader type
         */
        FRAGMENT = 1,
        /**
         * Geometry shader type
         */
        GEOMETRY = 2
    };

    enum class ShaderOperationResult : int {
        /**
         * Error happened during a shader processing operation
         */
        FAILURE = -1,
        /**
         * Success result during a shader processing operation
         */
        SUCCESS = 0
    };

    enum class ShaderBuiltInType : int { BASIC = 0, BASIC_2, STANDARD, MIXED_TEXTURE };
} // namespace ZEngine::Rendering::Shaders

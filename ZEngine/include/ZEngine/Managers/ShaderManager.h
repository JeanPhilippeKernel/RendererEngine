#pragma once
#include <filesystem>

#include <Managers/IAssetManager.h>
#include <Rendering/Shaders/Shader.h>

namespace ZEngine::Managers {

    class ShaderManager : public IAssetManager<Rendering::Shaders::Shader> {
    public:
        ShaderManager();
        virtual ~ShaderManager() = default;

        /**
         * Add a shader to the Shader manager store
         *
         * @param name Name of the shader. This name must be unique in the entire store
         * @param filename Path to find the shader source file in the system
         * @return A shader instance
         */
        Ref<Rendering::Shaders::Shader> Add(const char* name, const char* filename) override;

        /**
         * Add a shader to the Shader manager store
         *
         * @param filename Path to find the shader source file in the system
         * @return A shader instance
         */
        Ref<Rendering::Shaders::Shader> Load(const char* filename) override;
    };
} // namespace ZEngine::Managers

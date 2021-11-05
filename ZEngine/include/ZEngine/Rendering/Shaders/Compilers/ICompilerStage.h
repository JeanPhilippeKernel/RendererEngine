#pragma once
#include <string>
#include <vector>
#include <Rendering/Shaders/ShaderInformation.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>

namespace ZEngine::Rendering::Shaders::Compilers {
    
    class ShaderCompiler;

    struct StageInformation {
        bool IsSuccess{true};  //We assume default result of stage is true
        std::string ErrorMessage;
    };
    
    struct ICompilerStage {

        ICompilerStage() = default;
        virtual ~ICompilerStage() = default;

        virtual void SetContext(ShaderCompiler* const context) {
            if (context) {
                m_compiler_context = context;
            }
        }

        virtual void Run(std::vector<ShaderInformation>&) = 0;
        virtual void Next();
        virtual bool HasNext();

        const StageInformation& GetInformation() const { return m_information; }
    
    protected:
        ShaderCompiler*         m_compiler_context{ nullptr };  //BackReference to ShaderCompiler : we should update to WeakRef<...>
        StageInformation        m_information{};
        Ref<ICompilerStage>     m_next_stage{ nullptr };
    };
}
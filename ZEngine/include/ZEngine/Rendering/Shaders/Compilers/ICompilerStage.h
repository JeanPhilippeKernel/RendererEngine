#pragma once
#include <string>
#include <vector>
#include <Rendering/Shaders/ShaderInformation.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>

namespace ZEngine::Rendering::Shaders::Compilers {
    
    class ShaderCompiler;

    struct StageInformation {
		/**
		* A boolean that describes whether the stage was a success or not.
		*/
        bool IsSuccess{true};  

		/**
		* A log information that describes errors happened during the stage.
		*/
        std::string ErrorMessage;
    };
    
    struct ICompilerStage {

		/**
		* Initialize a new ICompilerStage instance.
		*/
        ICompilerStage() = default;
        virtual ~ICompilerStage() = default;

		/**
		* Set context
		* 
		* @param context A back reference to a ShaderCompiler
		*/               
        virtual void SetContext(ShaderCompiler* const context) {
            if (context) {
                m_compiler_context = context;
            }
        }

		/**
		* Run Compiler stage
		* 
		* @param information Collection of shader information
		*/                
        virtual void Run(std::vector<ShaderInformation>& information) = 0;

		/**
		* Update context to the next compiler stage
		*/                
        virtual void Next();

		/**
		* Check if the stage has a next stage that should be run
		* 
		* @return True or False
		*/                
        virtual bool HasNext();

		/**
		* Return information related to the stage. 
		* These information are updated during call of Run() 
        *
		* @return An information related to the compiler stage
		*/ 
        const StageInformation& GetInformation() const { return m_information; }
    
    protected:
        ShaderCompiler*         m_compiler_context{ nullptr };
        StageInformation        m_information{};
        Ref<ICompilerStage>     m_next_stage{ nullptr };
    };
}
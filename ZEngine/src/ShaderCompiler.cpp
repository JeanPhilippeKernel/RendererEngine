#include <pch.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    std::unordered_map<std::string, std::vector<ShaderInformation>> ShaderCompiler::s_already_compiled_shaders_collection;

    ShaderCompiler::ShaderCompiler() {
        m_reader = std::make_unique<ShaderReader>();
        m_stage  = std::make_shared<CompilationStage>();
        m_stage->SetContext(this);
    }

    ShaderCompiler::ShaderCompiler(std::string_view filename) : m_source_file(filename) {
        m_reader = std::make_unique<ShaderReader>();
        m_stage  = std::make_shared<CompilationStage>();
        m_stage->SetContext(this);
    }

    ShaderCompiler::~ShaderCompiler() {}

    void ShaderCompiler::SetSource(std::string_view filename) {
        m_source_file = filename.data();
    }

    std::tuple<ShaderOperationResult, GLuint> ShaderCompiler::Compile() {
        bool compile_process_succeeded{true};

        ZENGINE_CORE_INFO("====== Compilation process started ======");

        std::vector<ShaderInformation> shader_information;

        auto find_it = std::find_if(std::begin(s_already_compiled_shaders_collection), std::end(s_already_compiled_shaders_collection),
            [this](const auto& item) { return item.first == this->m_source_file; });

        if (find_it != std::end(s_already_compiled_shaders_collection)) {
            shader_information = find_it->second;
            std::for_each(std::begin(shader_information), std::end(shader_information), [](ShaderInformation& item) { item.CompiledOnce = true; });
        } else {
            ShaderOperationResult read_operation = m_reader->ReadAsync(m_source_file).get();
            if (read_operation == ShaderOperationResult::FAILURE) {
                ZENGINE_CORE_CRITICAL("Compilation process stopped");
                return std::make_tuple(ShaderOperationResult::FAILURE, 0);
            }

            shader_information = m_reader->GetInformations();
            if (shader_information.empty()) {
                ZENGINE_CORE_CRITICAL("Information collected while reading shader file are incorrect or not enough to continue compilation process");
                ZENGINE_CORE_CRITICAL("Compilation process stopped");
                return std::make_tuple(ShaderOperationResult::FAILURE, 0);
            }
        }

        while (m_running_stages) {

            ICompilerStage* stage = reinterpret_cast<ICompilerStage*>(m_stage.get());
            stage->Run(shader_information);

            const auto& stage_info    = stage->GetInformation();
            compile_process_succeeded = compile_process_succeeded && stage_info.IsSuccess;

            if (stage_info.IsSuccess && m_stage->HasNext()) {
                m_stage->Next();
            } else {
                m_running_stages = false;
            }
        }

        if (!compile_process_succeeded) {
            ZENGINE_CORE_CRITICAL("Compilation process weren't able to create a valid Shader Program");
            return std::make_tuple(ShaderOperationResult::FAILURE, 0);
        }

        ZENGINE_CORE_INFO("====== Compilation process succeeded ======");

        // We store it, so next time we won't run the compilation stage if it has been before
        s_already_compiled_shaders_collection.emplace(m_source_file, shader_information);

        const auto& first = std::begin(shader_information);
        return std::make_tuple(ShaderOperationResult::SUCCESS, first->ProgramId);
    }
} // namespace ZEngine::Rendering::Shaders::Compilers

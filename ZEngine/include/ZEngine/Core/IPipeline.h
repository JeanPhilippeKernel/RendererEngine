#pragma once
#include <string>
#include <ZEngineDef.h>

namespace ZEngine::Core {

    struct IPipelineContext;
    struct IPipelineStage;

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

    struct IPipelineStage {
        /**
         * Initialize a new IPipelineStage instance.
         */
        IPipelineStage()          = default;
        virtual ~IPipelineStage() = default;

        /**
         * Set context
         *
         * @param context A back reference to a ShaderCompiler
         */
        virtual void SetContext(IPipelineContext* const context);

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

    protected:
        StageInformation    m_information{};
        IPipelineContext*                  m_context{nullptr};
        Ref<IPipelineStage> m_next_stage{nullptr};
    };

    struct IPipelineContext {
        IPipelineContext() = default;
        virtual ~IPipelineContext() = default;

        /**
         * Update the current compiler stage
         * @param stage Compiler stage
         */
        virtual void UpdateStage(const Ref<IPipelineStage>& stage);

        /**
         * Update the current compiler stage
         * @param stage Compiler stage
         */
        virtual void UpdateStage(IPipelineStage* const stage);

    protected:
        Ref<IPipelineStage> m_stage{nullptr};
    };
} // namespace ZEngine::Core
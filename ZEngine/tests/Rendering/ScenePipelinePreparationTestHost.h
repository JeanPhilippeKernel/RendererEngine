#pragma once
#include <ZEngine/Rendering/Renderers/ScenePipelinePreparation.h>

namespace ZEngine::Tests
{
    /// Explicit CPU-only host. No missing graph/device is treated as readiness
    /// by production code; handoff tests supply this controlled service instead.
    struct ScenePipelinePreparationTestHost final : Rendering::Renderers::IScenePipelinePreparation
    {
        Rendering::Renderers::RGScenePipelinePreparationState State                   = Rendering::Renderers::RGScenePipelinePreparationState::Ready;
        Rendering::Renderers::RGScenePipelineUsage            Usage                   = Rendering::Renderers::RGScenePipelineUsage::None;
        uint32_t                                              BeginCalls              = 0;
        uint32_t                                              PollCalls               = 0;
        uint32_t                                              EndCalls                = 0;
        bool                                                  Accept                  = true;
        bool                                                  Active                  = false;
        Rendering::Renderers::RGScenePipelinePreparationState DescriptorState         = Rendering::Renderers::RGScenePipelinePreparationState::Ready;
        uint32_t                                              DescriptorPollCalls     = 0;
        bool                                                  ActivateDescriptors     = true;
        bool                                                  EnvironmentSourcesValid = false;

        bool                                                  BeginScenePipelinePreparation(Rendering::Renderers::RGScenePipelineUsage usage) override
        {
            ++BeginCalls;
            if (!Accept || Active)
            {
                return false;
            }
            Usage  = usage;
            Active = true;
            return true;
        }
        Rendering::Renderers::RGScenePipelinePreparationState PollScenePipelinePreparation(uint32_t = 1) override
        {
            ++PollCalls;
            return Active ? State : Rendering::Renderers::RGScenePipelinePreparationState::Idle;
        }
        void EndScenePipelinePreparation() override
        {
            ++EndCalls;
            Active = false;
        }
        Rendering::Renderers::RGScenePipelinePreparationState PollSceneDescriptorPreparation(const Rendering::Scenes::PreparedSceneFrame&, uint32_t = 1, const Rendering::Renderers::SceneDescriptorEnvironment& environment = {}) override
        {
            ++DescriptorPollCalls;
            EnvironmentSourcesValid = environment.SourceRadiance && *environment.SourceRadiance && environment.DiffuseIrradiance && *environment.DiffuseIrradiance && environment.SpecularEnvironment && *environment.SpecularEnvironment && environment.BrdfIntegrationLut && *environment.BrdfIntegrationLut;
            return Active ? DescriptorState : Rendering::Renderers::RGScenePipelinePreparationState::Idle;
        }
        bool ActivateSceneDescriptors() override
        {
            return Active && ActivateDescriptors;
        }
    };
} // namespace ZEngine::Tests

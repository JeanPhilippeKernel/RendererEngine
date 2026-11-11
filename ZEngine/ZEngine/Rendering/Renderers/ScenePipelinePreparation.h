#pragma once
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Scenes/PreparedSceneFrame.h>
#include <cstdint>

namespace ZEngine::Rendering::Renderers
{
    enum class RGScenePipelineUsage : uint8_t
    {
        None                = 0,
        Core                = 1 << 0,
        Sphere              = 1 << 1,
        HDRI                = 1 << 2,
        Atmosphere          = 1 << 3,
        EnvironmentLighting = 1 << 4,
        AtmosphereView      = 1 << 5,
    };

    constexpr RGScenePipelineUsage operator|(RGScenePipelineUsage lhs, RGScenePipelineUsage rhs)
    {
        return static_cast<RGScenePipelineUsage>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    enum class RGScenePipelinePreparationState : uint8_t
    {
        Idle,
        Preparing,
        Ready,
        Failed,
    };
    enum class RGSceneDescriptorProfile : uint8_t
    {
        None,
        Geometry,
        Culling,
        Camera,
        Transparent,
        EnvironmentBackground,
        Lighting,
        ToneMapping,
        SkyViewLut,
        AerialPerspective,
        SkyComposite,
        Grid,
        GridLabels,
        SelectionOutline
    };

    /// Borrowed only during one render-owner preparation pump. Each successful
    /// descriptor write clones its exact lease into the unpublished bank.
    struct SceneDescriptorEnvironment
    {
        const RenderResourceManager::TextureVersionPin* SourceRadiance            = nullptr;
        const RenderResourceManager::TextureVersionPin* DiffuseIrradiance         = nullptr;
        const RenderResourceManager::TextureVersionPin* SpecularEnvironment       = nullptr;
        const RenderResourceManager::TextureVersionPin* BrdfIntegrationLut        = nullptr;
        const RenderResourceManager::TextureVersionPin* AtmosphereTransmittance   = nullptr;
        const RenderResourceManager::TextureVersionPin* AtmosphereMultiscattering = nullptr;
        bool                                            UseCompositedSceneColor   = false;
        const RenderResourceManager::TextureVersionPin* GridLabelFont             = nullptr;
        std::span<const Core::Memory::BufferView>       SelectionTokenBuffers     = {};
        bool                                            GridLabelsEnabled         = false;
    };

    /// Render-owner host service. A successful Begin owns one transaction until
    /// End, including the interval between readiness, activation and recording.
    struct IScenePipelinePreparation
    {
        virtual ~IScenePipelinePreparation()                                                                                                                                                         = default;
        virtual bool                            BeginScenePipelinePreparation(RGScenePipelineUsage usage)                                                                                            = 0;
        virtual RGScenePipelinePreparationState PollScenePipelinePreparation(uint32_t max_variants = 1)                                                                                              = 0;
        virtual void                            EndScenePipelinePreparation()                                                                                                                        = 0;
        virtual RGScenePipelinePreparationState PollSceneDescriptorPreparation(const Scenes::PreparedSceneFrame& frame, uint32_t max_frames = 1, const SceneDescriptorEnvironment& environment = {}) = 0;
        virtual bool                            ActivateSceneDescriptors()                                                                                                                           = 0;
    };
} // namespace ZEngine::Rendering::Renderers

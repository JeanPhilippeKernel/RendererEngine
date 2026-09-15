#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <cmath>

namespace ZEngine::Rendering
{
    class RenderResourceManager;
}

namespace ZEngine::Rendering::Scenes
{
    /// @brief Direct-normal illuminance of the default clear-sky noon sun.
    inline constexpr float        StandardNoonSunIlluminanceLux = 120000.0f;

    /// @brief Illuminance represented by one scene-linear radiance unit.
    /// @details This calibrated unit bridge is used only by analytic
    /// atmosphere source capture and its IBL. It is not a display-exposure
    /// control: the default noon sun consequently maps to 20 scene units.
    inline constexpr float        ReferenceSunIlluminanceLux    = 6000.0f;

    [[nodiscard]] constexpr float ConvertSunIlluminanceToSceneRadiance(float illuminance_lux)
    {
        return illuminance_lux / ReferenceSunIlluminanceLux;
    }

    /// @brief CPU reference values for the static atmosphere multiscattering contract.
    /// @details The LUT stores an angular radiance integral per direct solar
    /// radiance. GPU consumers multiply it by IsotropicPhaseNormalization once.
    namespace AtmosphereScatteringContract
    {
        inline constexpr float        AngularIntegralScale        = 12.566370614359172f;
        inline constexpr float        IsotropicPhaseNormalization = 0.07957747154594767f;
        inline constexpr float        RetainedLightFraction       = 0.5f;
        inline constexpr float        MaximumReturnProbability    = 0.95f;

        [[nodiscard]] constexpr float MakeAngularIntegral(float single_scatter_fraction)
        {
            const float fraction           = single_scatter_fraction < 0.0f ? 0.0f : (single_scatter_fraction > 1.0f ? 1.0f : single_scatter_fraction);
            const float return_probability = fraction * RetainedLightFraction < MaximumReturnProbability ? fraction * RetainedLightFraction : MaximumReturnProbability;
            return AngularIntegralScale * fraction * return_probability / (1.0f - return_probability);
        }
    } // namespace AtmosphereScatteringContract

    struct GridConfig
    {
        float CellSize      = 0.025f;
        float FadeRadius    = 500.0f;
        float FadeStrength  = 0.5f;
        float LineWidth     = 1.5f;
        float GroundY       = 0.0f;
        int   MaxLOD        = 5;
        float ColorThin[4]  = {0.6f, 0.6f, 0.6f, 1.0f};
        float ColorThick[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float ColorXAxis[4] = {0.9f, 0.2f, 0.2f, 1.0f};
        float ColorZAxis[4] = {0.2f, 0.4f, 1.0f, 1.0f};
        bool  Enabled       = true;
    };

    /// @brief Selects the authored source used by a scene's active sky.
    enum class SkyMode : uint8_t
    {
        Atmosphere = 0,
        HDRI       = 1,
        SkySphere  = 2,
    };

    /// @brief Physical, scene-authored inputs for an analytic atmosphere.
    ///
    /// Distances are expressed in kilometres except PlanetCenterWorld, which is
    /// expressed in scene world units and converted using WorldUnitsPerMeter.
    struct AtmosphereSettings
    {
        // With the default one-world-unit-per-metre convention, editor scenes
        // are authored at the planet surface around world origin.
        float PlanetCenterWorld[3]              = {0.0f, -6360000.0f, 0.0f};
        float WorldUnitsPerMeter                = 1.0f;
        float PlanetRadiusKilometers            = 6360.0f;
        float AtmosphereRadiusKilometers        = 6460.0f;
        float RayleighScatteringPerKilometer[3] = {0.005802f, 0.013558f, 0.033100f};
        float RayleighScaleHeightKilometers     = 8.0f;
        float MieScatteringPerKilometer         = 0.003996f;
        float MieAbsorptionPerKilometer         = 0.004440f;
        float MieScaleHeightKilometers          = 1.2f;
        float MieAnisotropy                     = 0.8f;
        float OzoneAbsorptionPerKilometer[3]    = {0.000650f, 0.001881f, 0.000085f};
        float OzoneCenterKilometers             = 25.0f;
        float OzoneThicknessKilometers          = 15.0f;
        float SunAngularRadiusRadians           = 0.00465f;
        /// @brief Direct-normal solar illuminance in lux.
        /// @details It is converted to scene-linear radiance at the renderer
        /// boundary; display exposure remains a per-view concern.
        float SunIlluminanceLux                 = StandardNoonSunIlluminanceLux;
        /// @brief Diffuse albedo of the implicit planet surface.
        /// @details The atmosphere closes rays that reach the planet against
        /// this Lambertian surface only where scene geometry is absent.
        float GroundAlbedo[3]                   = {0.18f, 0.18f, 0.18f};
        /// @brief Scene-linear diffuse fill irradiance for the implicit ground.
        /// @details It prevents a no-terrain editor viewport from exposing a
        /// black lower hemisphere. Set it to zero for a fully unlit planet.
        float GroundAmbientIrradiance           = 0.5f;
    };

    /// @brief Artistic inputs for the analytic SkySphere presentation mode.
    struct SkySphereSettings
    {
        float HorizonColor[4]             = {0.38f, 0.58f, 0.88f, 1.0f};
        float ZenithColor[4]              = {0.04f, 0.13f, 0.35f, 1.0f};
        float GroundColor[4]              = {0.08f, 0.08f, 0.10f, 1.0f};
        float SunDiscAngularRadiusRadians = 0.00465f;
        float SunDiscIntensity            = 1.0f;
        float HorizonSharpness            = 1.0f;
        bool  ShowSunDisc                 = true;
    };

    /// @brief Main-thread-resolved directional-light input for atmosphere baking.
    /// @details DirectionToLight points from the scene toward the celestial source.
    ///          It is runtime state and is deliberately not serialized with SkyConfig.
    struct SkyCelestialLight
    {
        float              DirectionToLight[3] = {0.0f, 1.0f, 0.0f};
        bool               IsAvailable         = false;

        [[nodiscard]] bool Matches(const SkyCelestialLight& other) const
        {
            return IsAvailable == other.IsAvailable && DirectionToLight[0] == other.DirectionToLight[0] && DirectionToLight[1] == other.DirectionToLight[1] && DirectionToLight[2] == other.DirectionToLight[2];
        }

        [[nodiscard]] bool IsValid() const
        {
            if (!IsAvailable)
                return true;

            const float length_squared = DirectionToLight[0] * DirectionToLight[0] + DirectionToLight[1] * DirectionToLight[1] + DirectionToLight[2] * DirectionToLight[2];
            return std::isfinite(length_squared) && length_squared > 1.0e-8f;
        }
    };

    /// @brief Scene-owned, serializable sky authoring data.
    ///
    /// This is deliberately free of GPU handles, generated cache paths, and
    /// per-frame state. EnvironmentMap and PrimaryCelestialLight are stable
    /// UUID references; a nil UUID requests the renderer's documented fallback.
    struct SkyConfig
    {
        SkyMode            Mode                  = SkyMode::Atmosphere;
        uuids::uuid        EnvironmentMap        = {};
        float              EnvironmentIntensity  = 1.0f;
        float              EnvironmentTint[4]    = {1.0f, 1.0f, 1.0f, 1.0f};
        float              EnvironmentYawRadians = 0.0f;
        uuids::uuid        PrimaryCelestialLight = {};
        AtmosphereSettings Atmosphere            = {};
        SkySphereSettings  Sphere                = {};

        [[nodiscard]] bool IsHDRI() const
        {
            return Mode == SkyMode::HDRI;
        }
        [[nodiscard]] bool IsAtmosphere() const
        {
            return Mode == SkyMode::Atmosphere;
        }
        [[nodiscard]] bool IsSkySphere() const
        {
            return Mode == SkyMode::SkySphere;
        }

        /// @brief Returns whether all numeric authoring inputs are usable.
        [[nodiscard]] bool IsValid() const
        {
            const auto finite              = [](float value) { return std::isfinite(value); };
            const auto finite_non_negative = [finite](float value) { return finite(value) && value >= 0.0f; };
            const auto finite_rgb          = [finite_non_negative](const float (&value)[3]) { return finite_non_negative(value[0]) && finite_non_negative(value[1]) && finite_non_negative(value[2]); };
            const auto finite_rgba         = [finite_non_negative](const float (&value)[4]) { return finite_non_negative(value[0]) && finite_non_negative(value[1]) && finite_non_negative(value[2]) && finite_non_negative(value[3]); };

            if (static_cast<uint8_t>(Mode) > static_cast<uint8_t>(SkyMode::SkySphere) || !finite_non_negative(EnvironmentIntensity) || !finite_rgba(EnvironmentTint) || !finite(EnvironmentYawRadians))
                return false;

            const auto& atmosphere = Atmosphere;
            if (!finite(atmosphere.PlanetCenterWorld[0]) || !finite(atmosphere.PlanetCenterWorld[1]) || !finite(atmosphere.PlanetCenterWorld[2]) || !finite(atmosphere.WorldUnitsPerMeter) || atmosphere.WorldUnitsPerMeter <= 0.0f || !finite(atmosphere.PlanetRadiusKilometers) || atmosphere.PlanetRadiusKilometers <= 0.0f || !finite(atmosphere.AtmosphereRadiusKilometers) || atmosphere.AtmosphereRadiusKilometers <= atmosphere.PlanetRadiusKilometers || !finite_rgb(atmosphere.RayleighScatteringPerKilometer) || !finite(atmosphere.RayleighScaleHeightKilometers) ||
                atmosphere.RayleighScaleHeightKilometers <= 0.0f || !finite_non_negative(atmosphere.MieScatteringPerKilometer) || !finite_non_negative(atmosphere.MieAbsorptionPerKilometer) || !finite(atmosphere.MieScaleHeightKilometers) || atmosphere.MieScaleHeightKilometers <= 0.0f || !finite(atmosphere.MieAnisotropy) || atmosphere.MieAnisotropy <= -0.999f || atmosphere.MieAnisotropy >= 0.999f || !finite_rgb(atmosphere.OzoneAbsorptionPerKilometer) || !finite_non_negative(atmosphere.OzoneCenterKilometers) || !finite(atmosphere.OzoneThicknessKilometers) ||
                atmosphere.OzoneThicknessKilometers <= 0.0f || !finite_non_negative(atmosphere.SunAngularRadiusRadians) || !finite_non_negative(atmosphere.SunIlluminanceLux) || !finite_rgb(atmosphere.GroundAlbedo) || !finite_non_negative(atmosphere.GroundAmbientIrradiance))
                return false;

            const auto& sphere = Sphere;
            return finite_rgba(sphere.HorizonColor) && finite_rgba(sphere.ZenithColor) && finite_rgba(sphere.GroundColor) && finite_non_negative(sphere.SunDiscAngularRadiusRadians) && finite_non_negative(sphere.SunDiscIntensity) && finite(sphere.HorizonSharpness) && sphere.HorizonSharpness > 0.0f;
        }

        /// @brief Replaces malformed serialized/editor data with the safe default.
        void Sanitize()
        {
            if (!IsValid())
                *this = {};
        }
    };

    struct GpuDirectionalLight
    {
        ZEngine::Core::Maths::Vec4f Direction = {};
        ZEngine::Core::Maths::Vec4f Color     = {};
        float                       Intensity = 0.f;
        float                       _pad[3]   = {};
    };

    struct GpuPointLight
    {
        ZEngine::Core::Maths::Vec4f Position  = {};
        ZEngine::Core::Maths::Vec4f Color     = {};
        float                       Intensity = 0.f;
        float                       Radius    = 0.f;
        float                       _pad[2]   = {};
    };

    struct LightArrayUBO
    {
        GpuDirectionalLight DirectionalLights[4] = {};
        GpuPointLight       PointLights[8]       = {};
        uint32_t            DirectionalCount     = 0;
        uint32_t            PointCount           = 0;
        uint32_t            _pad[2]              = {};
    };

    // std430-compatible input record for GPU frustum culling. The compute shader
    // copies Command verbatim and changes instanceCount for culled entries only.
    struct FrustumCullingInput
    {
        Core::Maths::Vec4f    WorldBounds = {}; // xyz = world-space sphere center, w <= 0 means always visible
        VkDrawIndirectCommand Command     = {};
    };

    // Explicit tail padding makes this exactly match the GLSL push-constant block.
    struct FrustumCullingPushConstants
    {
        Core::Maths::Vec4f FrustumPlanes[6] = {};
        uint32_t           DrawCount        = 0;
        uint32_t           Padding[3]       = {};
    };

    static_assert(sizeof(FrustumCullingInput) == 32, "FrustumCullingInput must match its std430 GLSL representation");
    static_assert(sizeof(FrustumCullingPushConstants) == 112, "FrustumCullingPushConstants must match its GLSL representation");

    struct SceneData
    {
        static constexpr uint32_t   MAX_FRAMES_IN_FLIGHT                        = 3;
        static constexpr uint32_t   MAX_DRAW_COMMANDS                           = 8192;

        // Camera UBO — migrated to PerFrameUploadHeap; offset updated each frame in DrawScene
        uint32_t                    CameraHeapOffset                            = 0;

        // CPU-built draw candidates. The compute pass writes a matching indirect
        // array with instanceCount = 0 for frustum-culled candidates.
        uint32_t                    IndirectCommandCount                        = 0;
        FrustumCullingPushConstants CullingPushConstants                        = {};

        // Per-frame buffers prevent CPU uploads for frame N + 1 from racing GPU
        // reads issued for an earlier in-flight frame.
        Core::Memory::BufferView    TransformBuffers[MAX_FRAMES_IN_FLIGHT]      = {};
        Core::Memory::BufferView    MaterialBuffers[MAX_FRAMES_IN_FLIGHT]       = {};
        Core::Memory::BufferView    RenderDataBuffers[MAX_FRAMES_IN_FLIGHT]     = {};
        Core::Memory::BufferView    LightBuffers[MAX_FRAMES_IN_FLIGHT]          = {};

        // Per-frame ownership prevents frame N + 1 compute from overwriting the
        // commands still consumed by graphics for frame N.
        Core::Memory::BufferView    CullingInputBuffers[MAX_FRAMES_IN_FLIGHT]   = {};
        Core::Memory::BufferView    CulledIndirectBuffers[MAX_FRAMES_IN_FLIGHT] = {};

        // RRM vertex buffer handle — index buffer is paired via RRM::GetIndexBuffer(RMMVertexHandle).
        Rendering::BufferHandle     RMMVertexHandle                             = {};
    };
    ZDEFINE_PTR(SceneData);

    // One placed instance of a mesh asset in the scene.
    // Every drag-drop creates a new MeshInstance — even the same mesh dropped
    // twice produces two independent instances with separate transforms.
    struct MeshInstance
    {
        Core::Maths::Mat4f Transform = {}; // 64 bytes — owns its own cache line
        uuids::uuid        MeshUUID  = {}; // 16 bytes
        uint32_t           Id        = 0;
        char               Name[128] = {};
    };

    // Seqlock-protected scene state.
    //   Main thread:   Add/Remove/SetTransform + MarkInstancesDirty
    //   Render thread: GetInstancesSnapshot (spin-wait when seq is odd)
    //
    // Sequence counter convention:
    //   even → data stable (safe to snapshot)
    //   odd  → write in progress (reader spins)
    //
    // Arena allocators never free: even a "torn" pointer to Instances.m_data
    // points at still-valid memory, so retry-on-mismatch is safe.
    struct RenderScene
    {
        Core::Containers::Array<MeshInstance> Instances          = {};
        Core::Memory::ArenaAllocator          InstanceArena      = {};
        uint32_t                              NextInstanceId     = 1;

        SkyConfig                             Sky                = {};
        /// @brief Resolved from Sky.PrimaryCelestialLight by LightSyncSystem.
        SkyCelestialLight                     CelestialLight     = {};
        GridConfig                            Grid               = {};
        LightArrayUBO                         PendingLights      = {};

        PaddedAtomic<uint64_t>                m_seq              = {};
        PaddedAtomic<int32_t>                 SelectedInstanceId = {};
        PaddedAtomic<bool>                    InstancesDirty[3]  = {};
        // Incremented by the main/editor thread after changing Sky. The copied
        // config and this revision travel together through RenderFrameState, so
        // the render thread never reads mutable scene-owned sky data directly.
        PaddedAtomic<uint64_t>                SkyRevision        = {.value = 1};
        PaddedAtomic<bool>                    GridDirty[3]       = {};

        uint32_t                              AddMeshInstance(const uuids::uuid& uuid, const char* name);
        void                                  RemoveMeshInstance(uint32_t id, ZEngine::Rendering::RenderResourceManager* rrm = nullptr);
        void                                  SetInstanceTransform(uint32_t id, const Core::Maths::Mat4f& t);
        void                                  MarkInstancesDirty();
        void                                  MarkSkyDirty();

        // Fills `out` with a consistent copy; retries if a write was in progress.
        void                                  GetInstancesSnapshot(Core::Memory::ArenaAllocator* scratch, Core::Containers::Array<MeshInstance>& out) const;

    protected:
        void SeqBeginWrite();
        void SeqEndWrite();
    };
    ZDEFINE_PTR(RenderScene);

} // namespace ZEngine::Rendering::Scenes

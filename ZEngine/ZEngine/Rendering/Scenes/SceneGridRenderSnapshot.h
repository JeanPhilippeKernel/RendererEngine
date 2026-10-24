#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Maths/Quaternion.h>
#include <cstdint>

namespace ZEngine::ECS
{
    struct SceneGridSettings;
}

namespace ZEngine::Rendering::Scenes
{
    /// Immutable value-only Grid input, copied with the camera into the render mailbox.
    /// No scene, ECS, editor, allocator or GPU-resource ownership crosses this boundary.
    struct SceneGridRenderSnapshot
    {
        Core::Maths::Vec3f                     Origin         = {};
        Core::Maths::Quaternion<float>         Orientation    = {};
        Core::Containers::FixedArray<float, 4> ColorThin      = {.6f, .6f, .6f, 1.f};
        Core::Containers::FixedArray<float, 4> ColorThick     = {.3f, .3f, .3f, 1.f};
        Core::Containers::FixedArray<float, 4> ColorUAxis     = {.9f, .2f, .2f, 1.f};
        Core::Containers::FixedArray<float, 4> ColorVAxis     = {.2f, .4f, 1.f, 1.f};
        float                                  CellSize       = .025f;
        float                                  FadeRadius     = 500.f;
        float                                  FadeStrength   = .5f;
        float                                  LineWidth      = 1.5f;
        uint32_t                               MajorLineEvery = 10;
        int32_t                                MaxLOD         = 5;
        uint32_t                               Labels         = 0;
        bool                                   Enabled        = true;
    };

    /// Validation/canonicalization failure preserves output, including unused vector lanes.
    [[nodiscard]] bool MakeSceneGridRenderSnapshot(const ECS::SceneGridSettings& settings, SceneGridRenderSnapshot& output);
    /// Semantic conversion for checked GPU packing; failure preserves output.
    [[nodiscard]] bool ReadSceneGridRenderSnapshot(const SceneGridRenderSnapshot& snapshot, ECS::SceneGridSettings& output);
} // namespace ZEngine::Rendering::Scenes

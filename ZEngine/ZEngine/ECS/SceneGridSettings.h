#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Maths/Quaternion.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ZEngine::ECS
{
    enum class SceneGridLabels : uint32_t
    {
        None = 0,
        UV   = 1,
    };

    /// Scene-authored reference plane. Selection/snap/tool state is deliberately absent.
    struct SceneGridSettings
    {
        bool                           Enabled        = true;
        Core::Maths::Vec3f             Origin         = {};
        Core::Maths::Quaternion<float> Orientation    = {};
        float                          BaseSpacing    = .025f;
        uint32_t                       MajorLineEvery = 10;
        std::array<float, 4>           MinorColor     = {.6f, .6f, .6f, 1.f};
        std::array<float, 4>           MajorColor     = {.3f, .3f, .3f, 1.f};
        std::array<float, 4>           UColor         = {.9f, .2f, .2f, 1.f};
        std::array<float, 4>           VColor         = {.2f, .4f, 1.f, 1.f};
        float                          LineWidth      = 1.5f;
        float                          FadeRadius     = 500.f;
        float                          FadeStrength   = .5f;
        uint32_t                       MaxLOD         = 5;
        SceneGridLabels                Labels         = SceneGridLabels::None;
    };

    [[nodiscard]] bool      ValidSceneGridSettings(const SceneGridSettings& value);
    /// Exact authored-field comparison; ignores object padding/unused vector lanes.
    [[nodiscard]] bool      EqualSceneGridSettings(const SceneGridSettings& left, const SceneGridSettings& right);
    /// Failure leaves value unchanged. Normalizes and canonicalizes quaternion sign/negative zero.
    [[nodiscard]] bool      CanonicalizeSceneGridSettings(SceneGridSettings& value);

    /// Versioned little-endian semantic scalars, never native struct/padding bytes.
    /// Fixed storage makes scene-setting history independent of YAML/heap lifetime.
    inline constexpr size_t SceneGridBinaryBytes = 128;
    using SceneGridBinary                        = Core::Containers::FixedArray<std::byte, SceneGridBinaryBytes>;
    /// Invalid input or wrong-sized storage preserves the entire output buffer.
    [[nodiscard]] bool EncodeSceneGridBinary(const SceneGridSettings& value, Core::Containers::ArrayView<std::byte> output);
    /// Malformed/truncated/unsupported input preserves output, including unused lanes.
    [[nodiscard]] bool DecodeSceneGridBinary(Core::Containers::ArrayView<const std::byte> input, SceneGridSettings& output);
} // namespace ZEngine::ECS

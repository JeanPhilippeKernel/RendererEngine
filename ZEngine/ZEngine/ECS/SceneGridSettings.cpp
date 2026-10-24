#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <cmath>

namespace ZEngine::ECS
{
    bool EqualSceneGridSettings(const SceneGridSettings& left, const SceneGridSettings& right)
    {
        return left.Enabled == right.Enabled && left.Origin.x == right.Origin.x && left.Origin.y == right.Origin.y && left.Origin.z == right.Origin.z && left.Orientation.x == right.Orientation.x && left.Orientation.y == right.Orientation.y && left.Orientation.z == right.Orientation.z && left.Orientation.w == right.Orientation.w && left.BaseSpacing == right.BaseSpacing && left.MajorLineEvery == right.MajorLineEvery && left.MinorColor == right.MinorColor && left.MajorColor == right.MajorColor && left.UColor == right.UColor && left.VColor == right.VColor &&
               left.LineWidth == right.LineWidth && left.FadeRadius == right.FadeRadius && left.FadeStrength == right.FadeStrength && left.MaxLOD == right.MaxLOD && left.Labels == right.Labels;
    }

    bool ValidSceneGridSettings(const SceneGridSettings& value)
    {
        const auto finite = [](float item) { return std::isfinite(item); };
        const auto color  = [&](const auto& items) {
            for (const auto item : items)
                if (!finite(item) || item < 0.f || item > 1.f)
                    return false;
            return true;
        };
        const auto&  q       = value.Orientation;
        const double length2 = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
        return finite(value.Origin.x) && finite(value.Origin.y) && finite(value.Origin.z) && finite(q.x) && finite(q.y) && finite(q.z) && finite(q.w) && length2 > 1.e-16 && finite(value.BaseSpacing) && value.BaseSpacing > 0.f && value.MajorLineEvery > 0 && color(value.MinorColor) && color(value.MajorColor) && color(value.UColor) && color(value.VColor) && finite(value.LineWidth) && value.LineWidth > 0.f && finite(value.FadeRadius) && value.FadeRadius > 0.f && finite(value.FadeStrength) && value.FadeStrength >= 0.f && value.MaxLOD <= 16 &&
               static_cast<uint32_t>(value.Labels) <= static_cast<uint32_t>(SceneGridLabels::UV);
    }

    bool CanonicalizeSceneGridSettings(SceneGridSettings& value)
    {
        if (!ValidSceneGridSettings(value))
            return false;
        auto&        q       = value.Orientation;
        const double length2 = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
        // Avoid accumulating round-off on repeated capture/save/load of float32 unit quaternions.
        if (std::abs(length2 - 1.) > 1.e-7)
        {
            const double inverse = 1. / std::sqrt(length2);
            q                    = {float(q.x * inverse), float(q.y * inverse), float(q.z * inverse), float(q.w * inverse)};
        }
        if (q.w < 0.f || (q.w == 0.f && (q.x < 0.f || (q.x == 0.f && (q.y < 0.f || (q.y == 0.f && q.z < 0.f))))))
            q = -q;
        if (q.x == 0.f)
            q.x = 0.f;
        if (q.y == 0.f)
            q.y = 0.f;
        if (q.z == 0.f)
            q.z = 0.f;
        if (q.w == 0.f)
            q.w = 0.f;
        return true;
    }

    bool EncodeSceneGridBinary(const SceneGridSettings& value, Core::Containers::ArrayView<std::byte> output)
    {
        auto canonical = value;
        if (!output.data() || output.size() != SceneGridBinaryBytes || !CanonicalizeSceneGridSettings(canonical))
            return false;
        SceneBinaryWriter writer({output.data(), output.size()});
        const auto        scalar  = [&](float item) { return writer.Float(item == 0.f ? 0.f : item); };
        const auto&       q       = canonical.Orientation;
        bool              encoded = writer.UInt32(1) && writer.UInt32(canonical.Enabled ? 1u : 0u) && scalar(canonical.Origin.x) && scalar(canonical.Origin.y) && scalar(canonical.Origin.z) && scalar(q.x) && scalar(q.y) && scalar(q.z) && scalar(q.w) && scalar(canonical.BaseSpacing) && writer.UInt32(canonical.MajorLineEvery);
        for (const auto* color : {canonical.MinorColor.data(), canonical.MajorColor.data(), canonical.UColor.data(), canonical.VColor.data()})
            for (uint32_t i = 0; i < 4; ++i)
                encoded = scalar(color[i]) && encoded;
        encoded = scalar(canonical.LineWidth) && scalar(canonical.FadeRadius) && scalar(canonical.FadeStrength) && writer.UInt32(canonical.MaxLOD) && writer.UInt32(uint32_t(canonical.Labels)) && encoded;
        // Validation and exact-size preflight make every scalar write infallible.
        ZENGINE_VALIDATE_ASSERT(encoded && writer.Remaining() == 0, "Grid binary schema size mismatch")
        return encoded && writer.Remaining() == 0;
    }

    bool DecodeSceneGridBinary(Core::Containers::ArrayView<const std::byte> input, SceneGridSettings& output)
    {
        if (!input.data() || input.size() != SceneGridBinaryBytes)
            return false;
        SceneBinaryReader reader({input.data(), input.size()});
        SceneGridSettings value   = {};
        uint32_t          version = 0, enabled = 0, labels = 0;
        auto&             q = value.Orientation;
        if (!reader.UInt32(version) || version != 1 || !reader.UInt32(enabled) || enabled > 1 || !reader.Vector(value.Origin) || !reader.Float(q.x) || !reader.Float(q.y) || !reader.Float(q.z) || !reader.Float(q.w) || !reader.Float(value.BaseSpacing) || !reader.UInt32(value.MajorLineEvery))
            return false;
        for (auto* color : {value.MinorColor.data(), value.MajorColor.data(), value.UColor.data(), value.VColor.data()})
            for (uint32_t i = 0; i < 4; ++i)
                if (!reader.Float(color[i]))
                    return false;
        if (!reader.Float(value.LineWidth) || !reader.Float(value.FadeRadius) || !reader.Float(value.FadeStrength) || !reader.UInt32(value.MaxLOD) || !reader.UInt32(labels) || reader.Remaining())
            return false;
        value.Enabled = enabled != 0;
        value.Labels  = SceneGridLabels(labels);
        if (!CanonicalizeSceneGridSettings(value))
            return false;
        output = value;
        return true;
    }
} // namespace ZEngine::ECS

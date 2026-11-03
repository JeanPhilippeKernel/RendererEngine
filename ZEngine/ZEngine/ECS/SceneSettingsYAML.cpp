#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/ECS/SceneSettingsFields.h>
#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <cmath>

namespace ZEngine::ECS
{
    namespace
    {
        using SceneSettingsDetail::FloatField;
        using SceneSettingsDetail::SkyAtmosphere;
        using SceneSettingsDetail::SkyEnvironment;
        using SceneSettingsDetail::SkyPresentation;
        using SceneSettingsDetail::SkySphere;

        bool Reject(SceneDiagnostics* diagnostics)
        {
            SceneDiagnosticError(diagnostics, "Invalid or unsupported scene Grid/sky settings.");
            return false;
        }

        bool ReadFloat(const SceneYAML::Node& node, float& out)
        {
            return ReadSceneYAMLFloat(node, out);
        }

        bool ReadUInt(const SceneYAML::Node& node, uint32_t& out)
        {
            return ReadSceneYAMLUInt(node, out);
        }

        bool ReadBool(const SceneYAML::Node& node, bool& out)
        {
            if (!node.IsScalar() || (node.Scalar() != "true" && node.Scalar() != "false"))
                return false;
            out = node.Scalar() == "true";
            return true;
        }

        bool ReadUUID(const SceneYAML::Node& node, uuids::uuid& out)
        {
            if (node.IsNull())
            {
                out = {};
                return true;
            }
            if (!node.IsScalar() || node.Scalar().size() != 36)
                return false;
            const auto parsed = uuids::uuid::from_string(node.Scalar());
            if (!parsed || parsed->is_nil())
                return false;
            out = *parsed;
            return true;
        }

        template <size_t Count>
        bool EncodeFloats(SceneYAML::Storage& storage, SceneYAML::Node result, const Core::Containers::FixedArray<FloatField, Count>& fields)
        {
            for (const auto& field : fields)
            {
                if (field.Count == 1)
                {
                    if (!storage.Set(result, field.Key, storage.Float(*field.Data)))
                        return false;
                }
                else
                {
                    const auto array = storage.Make(SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
                    for (size_t i = 0; i < field.Count; ++i)
                        if (!storage.Append(array, storage.Float(field.Data[i])))
                            return false;
                    if (!storage.Set(result, field.Key, array))
                        return false;
                }
            }
            return storage.Good();
        }

        template <size_t Count>
        bool DecodeFloats(const SceneYAML::Node& node, const Core::Containers::FixedArray<FloatField, Count>& fields, bool optional = false)
        {
            for (const auto& field : fields)
            {
                const auto data = FindSceneYAMLField(node, field.Key);
                if (optional && !data.IsDefined())
                    continue;
                if (field.Count == 1)
                {
                    if (!ReadFloat(data, *field.Data))
                        return false;
                }
                else
                {
                    if (!data.IsSequence() || data.size() != field.Count)
                        return false;
                    for (size_t i = 0; i < field.Count; ++i)
                        if (!ReadFloat(data[i], field.Data[i]))
                            return false;
                }
            }
            return true;
        }

        auto GridColors(SceneGridSettings& value)
        {
            return Core::Containers::FixedArray{
                FloatField{"minor_color", value.MinorColor.data(), 4},
                FloatField{"major_color", value.MajorColor.data(), 4},
                FloatField{    "u_color",     value.UColor.data(), 4},
                FloatField{    "v_color",     value.VColor.data(), 4},
            };
        }

    } // namespace

    bool EncodeSceneGridYAML(const SceneGridSettings& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
    {
        auto value = source;
        if (!out || !out->Owner() || !out->Owner()->Good() || !CanonicalizeSceneGridSettings(value))
            return Reject(diagnostics);
        auto&      storage     = *out->Owner();
        const auto node        = storage.Make(SceneYAML::NodeType::Map);
        const auto origin      = storage.Make(SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
        const auto orientation = storage.Make(SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
        for (const float item : {value.Origin.x, value.Origin.y, value.Origin.z})
            if (!storage.Append(origin, storage.Float(item)))
                return Reject(diagnostics);
        for (const float item : {value.Orientation.x, value.Orientation.y, value.Orientation.z, value.Orientation.w})
            if (!storage.Append(orientation, storage.Float(item)))
                return Reject(diagnostics);
        if (!storage.Set(node, "enabled", storage.Bool(value.Enabled)) || !storage.Set(node, "origin", origin) || !storage.Set(node, "orientation", orientation) || !storage.Set(node, "base_spacing", storage.Float(value.BaseSpacing)) || !storage.Set(node, "major_line_every", storage.UInt(value.MajorLineEvery)) || !EncodeFloats(storage, node, GridColors(value)) || !storage.Set(node, "line_width", storage.Float(value.LineWidth)) || !storage.Set(node, "fade_radius", storage.Float(value.FadeRadius)) ||
            !storage.Set(node, "fade_strength", storage.Float(value.FadeStrength)) || !storage.Set(node, "max_lod", storage.UInt(value.MaxLOD)) || !storage.Set(node, "labels", storage.UInt(static_cast<uint32_t>(value.Labels))))
            return Reject(diagnostics);
        *out = node;
        return true;
    }

    bool DecodeSceneGridYAML(const SceneYAML::Node& input, SceneGridSettings* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !input.IsDefined() || !input.IsMap() || input.size() != 14)
            return Reject(diagnostics);
        {
            SceneGridSettings value       = {};
            const auto        origin      = FindSceneYAMLField(input, "origin");
            const auto        orientation = FindSceneYAMLField(input, "orientation");
            uint32_t          labels      = 0;
            if (!origin.IsSequence() || origin.size() != 3 || !orientation.IsSequence() || orientation.size() != 4 || !ReadBool(FindSceneYAMLField(input, "enabled"), value.Enabled) || !ReadFloat(origin[0], value.Origin.x) || !ReadFloat(origin[1], value.Origin.y) || !ReadFloat(origin[2], value.Origin.z) || !ReadFloat(orientation[0], value.Orientation.x) || !ReadFloat(orientation[1], value.Orientation.y) || !ReadFloat(orientation[2], value.Orientation.z) || !ReadFloat(orientation[3], value.Orientation.w) ||
                !ReadFloat(FindSceneYAMLField(input, "base_spacing"), value.BaseSpacing) || !ReadUInt(FindSceneYAMLField(input, "major_line_every"), value.MajorLineEvery) || !DecodeFloats(input, GridColors(value)) || !ReadFloat(FindSceneYAMLField(input, "line_width"), value.LineWidth) || !ReadFloat(FindSceneYAMLField(input, "fade_radius"), value.FadeRadius) || !ReadFloat(FindSceneYAMLField(input, "fade_strength"), value.FadeStrength) || !ReadUInt(FindSceneYAMLField(input, "max_lod"), value.MaxLOD) || !ReadUInt(FindSceneYAMLField(input, "labels"), labels))
                return Reject(diagnostics);
            value.Labels = static_cast<SceneGridLabels>(labels);
            if (!CanonicalizeSceneGridSettings(value))
                return Reject(diagnostics);
            *out = value;
            return true;
        }
    }

    bool EncodeSceneSkyYAML(const Rendering::Scenes::SkyConfig& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !out->Owner() || !out->Owner()->Good() || !source.IsValid())
            return Reject(diagnostics);
        auto       value      = source;
        auto&      storage    = *out->Owner();
        const auto node       = storage.Make(SceneYAML::NodeType::Map);
        const auto atmosphere = storage.Make(SceneYAML::NodeType::Map);
        const auto sphere     = storage.Make(SceneYAML::NodeType::Map);
        if (!storage.Set(node, "mode", storage.UInt(static_cast<uint32_t>(value.Mode))) || !storage.Set(node, "environment_map", storage.UUID(value.EnvironmentMap)) || !EncodeFloats(storage, node, SkyEnvironment(value)) || !EncodeFloats(storage, node, SkyPresentation(value)) || !storage.Set(node, "primary_celestial_light", storage.UUID(value.PrimaryCelestialLight)) || !EncodeFloats(storage, atmosphere, SkyAtmosphere(value.Atmosphere)) || !EncodeFloats(storage, sphere, SkySphere(value.Sphere)) ||
            !storage.Set(sphere, "show_sun_disc", storage.Bool(value.Sphere.ShowSunDisc)) || !storage.Set(node, "atmosphere", atmosphere) || !storage.Set(node, "sphere", sphere))
            return Reject(diagnostics);
        *out = node;
        return true;
    }

    bool DecodeSceneSkyYAML(const SceneYAML::Node& input, Rendering::Scenes::SkyConfig* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !input.IsMap())
            return Reject(diagnostics);
        {
            Rendering::Scenes::SkyConfig value           = {};
            uint32_t                     mode            = 0;
            const auto                   atmosphere      = FindSceneYAMLField(input, "atmosphere");
            const auto                   sphere          = FindSceneYAMLField(input, "sphere");
            // Additive fields absent in older schema-2 scenes retain their authored
            // defaults. Present values are still strictly validated; unknown keys
            // are rejected rather than silently discarded on the next save.
            const auto                   presentation    = SkyPresentation(value);
            size_t                       expected_fields = 5 + SkyEnvironment(value).size();
            for (const auto& field : presentation)
                expected_fields += FindSceneYAMLField(input, field.Key).IsDefined() ? 1 : 0;
            if (input.size() != expected_fields || !DecodeFloats(input, presentation, true))
                return Reject(diagnostics);
            if (!atmosphere.IsMap() || atmosphere.size() != SkyAtmosphere(value.Atmosphere).size() || !sphere.IsMap() || sphere.size() != SkySphere(value.Sphere).size() + 1 || !ReadUInt(FindSceneYAMLField(input, "mode"), mode) || mode > static_cast<uint32_t>(Rendering::Scenes::SkyMode::SkySphere) || !ReadUUID(FindSceneYAMLField(input, "environment_map"), value.EnvironmentMap) || !ReadUUID(FindSceneYAMLField(input, "primary_celestial_light"), value.PrimaryCelestialLight) || !DecodeFloats(input, SkyEnvironment(value)) ||
                !DecodeFloats(atmosphere, SkyAtmosphere(value.Atmosphere)) || !DecodeFloats(sphere, SkySphere(value.Sphere)) || !ReadBool(sphere["show_sun_disc"], value.Sphere.ShowSunDisc))
                return Reject(diagnostics);
            value.Mode = static_cast<Rendering::Scenes::SkyMode>(mode);
            if (!value.IsValid())
                return Reject(diagnostics);
            *out = value;
            return true;
        }
    }
} // namespace ZEngine::ECS

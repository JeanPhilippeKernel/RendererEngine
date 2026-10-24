#include <ZEngine/ECS/SceneYamlValue.h>
#include <charconv>
#include <cmath>
#include <string_view>
#include <type_traits>

namespace ZEngine::ECS
{
    namespace
    {
        bool Reject(SceneDiagnostics* diagnostics)
        {
            SceneDiagnosticError(diagnostics, "Invalid YAML component field, scalar type, duplicate key or capacity.");
            return false;
        }

    } // namespace

    SceneYAML::Node FindSceneYAMLField(const SceneYAML::Node& node, std::string_view key)
    {
        return node[key];
    }

    bool ReadSceneYAMLFloat(const SceneYAML::Node& node, float& out)
    {
        if (!node.IsDefined() || !node.IsScalar())
            return false;
        std::string_view text = node.Scalar();
        if (text.starts_with('+'))
        {
            text.remove_prefix(1);
            if (text.starts_with('-'))
                return false;
        }
        if (text.empty() || text.front() == '+')
            return false;
        float      value  = 0.f;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
            return false;
        out = value;
        return true;
    }

    bool ReadSceneYAMLUInt(const SceneYAML::Node& node, uint32_t& out)
    {
        if (!node.IsDefined() || !node.IsScalar() || node.Scalar().empty())
            return false;
        const auto& text   = node.Scalar();
        uint32_t    value  = 0;
        const auto  result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return false;
        out = value;
        return true;
    }

    bool EncodeSceneValueYAML(const SceneValue& value, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !out->Owner() || !out->Owner()->Good())
            return Reject(diagnostics);
        auto& storage   = *out->Owner();
        auto  candidate = storage.Make(SceneYAML::NodeType::Map);
        for (const auto& field : value.Fields())
        {
            if (!field.Key || !field.Key[0] || candidate[field.Key].IsDefined())
                return Reject(diagnostics);
            SceneYAML::Node scalar;
            const bool      valid = std::visit(
                [&](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, Core::Maths::Vec3f>)
                    {
                        if (!std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.z))
                            return false;
                        scalar = storage.Make(SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
                        return storage.Append(scalar, storage.Float(item.x)) && storage.Append(scalar, storage.Float(item.y)) && storage.Append(scalar, storage.Float(item.z));
                    }
                    else if constexpr (std::is_same_v<T, uuids::uuid>)
                    {
                        scalar = storage.UUID(item);
                    }
                    else if constexpr (std::is_same_v<T, SceneStringValue>)
                    {
                        if ((!item.Data && item.Size) || item.Size > 4096)
                            return false;
                        const std::string_view text(item.Data ? item.Data : "", item.Size);
                        if (text.find('\0') != std::string_view::npos)
                            return false;
                        scalar = storage.Scalar(text);
                    }
                    else
                    {
                        if constexpr (std::is_same_v<T, float>)
                            if (!std::isfinite(item))
                                return false;
                        if constexpr (std::is_same_v<T, float>)
                            scalar = storage.Float(item);
                        else if constexpr (std::is_same_v<T, bool>)
                            scalar = storage.Bool(item);
                        else
                            scalar = storage.UInt(item);
                    }
                    return scalar.IsDefined();
                },
                field.Value);
            if (!valid)
                return Reject(diagnostics);
            if (!storage.Set(candidate, field.Key, scalar))
                return Reject(diagnostics);
        }
        out->reset(candidate);
        return true;
    }

    bool DecodeSceneValueYAML(const SceneValue& shape, const SceneYAML::Node& input, SceneValueWriter* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !out->Empty() || !input.IsDefined() || !input.IsMap() || input.size() != shape.Fields().size() || shape.Fields().size() > 64 || out->Remaining() < shape.Fields().size())
            return Reject(diagnostics);
        {
            // Public codecs may receive nodes not produced by the bounded document parser.
            uint64_t seen = 0;
            for (const auto& entry : input.Mapping())
            {
                size_t index = 0;
                for (; index < shape.Fields().size(); ++index)
                    if (shape.Fields()[index].Key && entry.Key == shape.Fields()[index].Key)
                        break;
                if (index == shape.Fields().size() || (seen & (uint64_t{1} << index)))
                    return Reject(diagnostics);
                seen |= uint64_t{1} << index;
            }
            auto candidate = *out;
            for (const auto& field : shape.Fields())
            {
                const auto node = FindSceneYAMLField(input, field.Key);
                if (!node.IsDefined())
                    return Reject(diagnostics);
                SceneScalar scalar = field.Value;
                const bool  valid  = std::visit(
                    [&](auto& item) {
                        using T = std::decay_t<decltype(item)>;
                        if constexpr (std::is_same_v<T, Core::Maths::Vec3f>)
                        {
                            return node.IsSequence() && node.size() == 3 && ReadSceneYAMLFloat(node[0], item.x) && ReadSceneYAMLFloat(node[1], item.y) && ReadSceneYAMLFloat(node[2], item.z);
                        }
                        else if constexpr (std::is_same_v<T, uuids::uuid>)
                        {
                            if (node.IsNull())
                            {
                                item = {};
                                return true;
                            }
                            if (!node.IsScalar() || node.Scalar().size() != 36)
                                return false;
                            const auto parsed = uuids::uuid::from_string(node.Scalar());
                            if (!parsed || parsed->is_nil())
                                return false;
                            item = *parsed;
                            return true;
                        }
                        else if constexpr (std::is_same_v<T, SceneStringValue>)
                        {
                            if (!node.IsScalar() || node.Scalar().size() > 4096 || node.Scalar().find('\0') != std::string::npos)
                                return false;
                            item = {node.Scalar().data(), static_cast<uint32_t>(node.Scalar().size())};
                            return true;
                        }
                        else if constexpr (std::is_same_v<T, float>)
                            return ReadSceneYAMLFloat(node, item);
                        else if constexpr (std::is_same_v<T, bool>)
                        {
                            if (!node.IsScalar() || (node.Scalar() != "true" && node.Scalar() != "false"))
                                return false;
                            item = node.Scalar() == "true";
                            return true;
                        }
                        else
                        {
                            return ReadSceneYAMLUInt(node, item);
                        }
                    },
                    scalar);
                if (!valid || !candidate.Add(field.Key, scalar))
                    return Reject(diagnostics);
            }
            *out = candidate;
            return true;
        }
    }
} // namespace ZEngine::ECS

#include <ZEngine/ECS/AuthoredComponentValues.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <array>

namespace ZEngine::ECS
{
    namespace
    {
        struct TransformCodec
        {
            using Component                           = Components::TransformComponent;
            using Value                               = LocalTransformValue;
            static constexpr cstring    Key           = "TransformComponent";
            static constexpr size_t     PayloadFields = 3;
            static constexpr size_t     BinaryBytes   = 9 * sizeof(float);
            static constexpr std::array Fields        = {
                SceneFieldSchema{"position"},
                SceneFieldSchema{"rotation"},
                SceneFieldSchema{"scale"},
                SceneFieldSchema{"previous_position", SceneFieldClass::RuntimeDerived},
                SceneFieldSchema{"world_transform", SceneFieldClass::RuntimeDerived},
            };

            static Value Capture(const Component& component)
            {
                return CaptureLocalTransform(component);
            }
            static bool Valid(const Value& value)
            {
                return ValidLocalTransform(value);
            }
            static void Apply(Component& component, const Value& value)
            {
                ApplyLocalTransform(component, value);
            }
            static bool Read(const SceneValue& payload, Value& value)
            {
                const auto* position = payload.Find<Core::Maths::Vec3f>("position");
                const auto* rotation = payload.Find<Core::Maths::Vec3f>("rotation");
                const auto* scale    = payload.Find<Core::Maths::Vec3f>("scale");
                if (payload.Fields().size() != PayloadFields || !position || !rotation || !scale)
                {
                    return false;
                }
                value = {*position, *rotation, *scale};
                return Valid(value);
            }
            static bool Write(const Value& value, SceneValueWriter& writer)
            {
                return writer.Add("position", value.Position) && writer.Add("rotation", value.Rotation) && writer.Add("scale", value.Scale);
            }
            static bool Encode(const Value& value, SceneBinaryWriter& writer)
            {
                return writer.Vector(value.Position) && writer.Vector(value.Rotation) && writer.Vector(value.Scale);
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                return reader.Vector(value.Position) && reader.Vector(value.Rotation) && reader.Vector(value.Scale) && Valid(value);
            }
        };

        struct LightCodec
        {
            using Component                           = Components::LightComponent;
            using Value                               = AuthoredLightValue;
            static constexpr cstring    Key           = "LightComponent";
            static constexpr size_t     PayloadFields = 5;
            static constexpr size_t     BinaryBytes   = sizeof(uint32_t) + 6 * sizeof(float);
            static constexpr std::array Fields        = {SceneFieldSchema{"type"}, SceneFieldSchema{"intensity"}, SceneFieldSchema{"range"}, SceneFieldSchema{"spot_angle"}, SceneFieldSchema{"color"}};

            static Value                Capture(const Component& component)
            {
                return CaptureAuthoredLight(component);
            }
            static bool Valid(const Value& value)
            {
                return ValidAuthoredLight(value);
            }
            static void Apply(Component& component, const Value& value)
            {
                ApplyAuthoredLight(component, value);
            }
            static bool Read(const SceneValue& payload, Value& value)
            {
                const auto* type      = payload.Find<uint32_t>("type");
                const auto* intensity = payload.Find<float>("intensity");
                const auto* range     = payload.Find<float>("range");
                const auto* angle     = payload.Find<float>("spot_angle");
                const auto* color     = payload.Find<Core::Maths::Vec3f>("color");
                if (payload.Fields().size() != PayloadFields || !type || *type > static_cast<uint32_t>(Component::Type::Spot) || !intensity || !range || !angle || !color)
                {
                    return false;
                }
                value = {static_cast<Component::Type>(*type), *intensity, *range, *angle, *color};
                return Valid(value);
            }
            static bool Write(const Value& value, SceneValueWriter& writer)
            {
                return writer.Add("type", static_cast<uint32_t>(value.Type)) && writer.Add("intensity", value.Intensity) && writer.Add("range", value.Range) && writer.Add("spot_angle", value.SpotAngle) && writer.Add("color", value.Color);
            }
            static bool Encode(const Value& value, SceneBinaryWriter& writer)
            {
                return writer.UInt32(static_cast<uint32_t>(value.Type)) && writer.Float(value.Intensity) && writer.Float(value.Range) && writer.Float(value.SpotAngle) && writer.Vector(value.Color);
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                uint32_t type = 0;
                if (!reader.UInt32(type) || type > static_cast<uint32_t>(Component::Type::Spot))
                {
                    return false;
                }
                value.Type = static_cast<Component::Type>(type);
                return reader.Float(value.Intensity) && reader.Float(value.Range) && reader.Float(value.SpotAngle) && reader.Vector(value.Color) && Valid(value);
            }
        };

        struct CameraCodec
        {
            using Component                           = Components::CameraComponent;
            using Value                               = AuthoredCameraValue;
            static constexpr cstring    Key           = "CameraComponent";
            static constexpr size_t     PayloadFields = 5;
            static constexpr size_t     BinaryBytes   = 4 * sizeof(float) + sizeof(uint32_t);
            static constexpr std::array Fields        = {SceneFieldSchema{"fov_y"}, SceneFieldSchema{"near"}, SceneFieldSchema{"far"}, SceneFieldSchema{"aspect_ratio"}, SceneFieldSchema{"is_main"}};

            static Value                Capture(const Component& component)
            {
                return CaptureAuthoredCamera(component);
            }
            static bool Valid(const Value& value)
            {
                return ValidAuthoredCamera(value);
            }
            static void Apply(Component& component, const Value& value)
            {
                ApplyAuthoredCamera(component, value);
            }
            static bool Read(const SceneValue& payload, Value& value)
            {
                const auto* fov    = payload.Find<float>("fov_y");
                const auto* near   = payload.Find<float>("near");
                const auto* far    = payload.Find<float>("far");
                const auto* aspect = payload.Find<float>("aspect_ratio");
                const auto* main   = payload.Find<bool>("is_main");
                if (payload.Fields().size() != PayloadFields || !fov || !near || !far || !aspect || !main)
                {
                    return false;
                }
                value = {*fov, *near, *far, *aspect, *main};
                return Valid(value);
            }
            static bool Write(const Value& value, SceneValueWriter& writer)
            {
                return writer.Add("fov_y", value.FovY) && writer.Add("near", value.Near) && writer.Add("far", value.Far) && writer.Add("aspect_ratio", value.AspectRatio) && writer.Add("is_main", value.IsMain);
            }
            static bool Encode(const Value& value, SceneBinaryWriter& writer)
            {
                return writer.Float(value.FovY) && writer.Float(value.Near) && writer.Float(value.Far) && writer.Float(value.AspectRatio) && writer.UInt32(value.IsMain ? 1u : 0u);
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                uint32_t main = 0;
                if (!reader.Float(value.FovY) || !reader.Float(value.Near) || !reader.Float(value.Far) || !reader.Float(value.AspectRatio) || !reader.UInt32(main) || main > 1)
                {
                    return false;
                }
                value.IsMain = main != 0;
                return Valid(value);
            }
        };

        template <typename Codec>
        struct Callbacks
        {
            static size_t CandidateMemoryBound(uint32_t count)
            {
                return ComponentStorage<typename Codec::Component>::AllocationUpperBound(count, EntityRegistry::MAX_ENTITIES);
            }
            static bool Reject(SceneDiagnostics* diagnostics)
            {
                SceneDiagnosticError(diagnostics, "Invalid built-in component value, version, target or codec capacity.");
                return false;
            }

            static bool WriteValue(const typename Codec::Value& value, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                if (!out || !out->Empty() || out->Remaining() < Codec::PayloadFields || !Codec::Valid(value))
                {
                    return Reject(diagnostics);
                }
                auto candidate = *out;
                if (!Codec::Write(value, candidate))
                {
                    return Reject(diagnostics);
                }
                *out = candidate;
                return true;
            }

            static bool Capture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                const auto* component = scene.IsAlive(entity) ? scene.GetComponent<typename Codec::Component>(entity) : nullptr;
                return component ? WriteValue(Codec::Capture(*component), out, diagnostics) : Reject(diagnostics);
            }

            static bool Populate(void*, EntityID entity, Scene& scene, const SceneValue& payload, SceneDiagnostics* diagnostics)
            {
                typename Codec::Value value = {};
                if (!scene.IsAlive(entity) || scene.HasComponent<typename Codec::Component>(entity) || !Codec::Read(payload, value))
                {
                    return Reject(diagnostics);
                }
                typename Codec::Component component = {};
                Codec::Apply(component, value);
                scene.AddComponent<typename Codec::Component>(entity, component);
                return true;
            }

            static bool Encode(void*, const SceneValue& payload, SceneBinaryWriter* writer, SceneDiagnostics* diagnostics)
            {
                typename Codec::Value value = {};
                if (!writer || writer->Remaining() < Codec::BinaryBytes || !Codec::Read(payload, value))
                {
                    return Reject(diagnostics);
                }
                auto candidate = *writer;
                if (!Codec::Encode(value, candidate))
                {
                    return Reject(diagnostics);
                }
                *writer = candidate;
                return true;
            }

            static bool Decode(void*, uint32_t version, SceneBinaryReader* reader, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                // The enclosing component table supplies an exactly bounded payload reader.
                if (version != 1 || !reader || reader->Remaining() != Codec::BinaryBytes)
                {
                    return Reject(diagnostics);
                }
                auto                  candidate = *reader;
                typename Codec::Value value     = {};
                if (!Codec::Decode(candidate, value))
                {
                    return Reject(diagnostics);
                }
                if (!WriteValue(value, out, diagnostics))
                {
                    return false;
                }
                *reader = candidate;
                return true;
            }

            static bool EncodeYAML(void*, const SceneValue& payload, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
            {
                typename Codec::Value value = {};
                return Codec::Read(payload, value) ? EncodeSceneValueYAML(payload, out, diagnostics) : Reject(diagnostics);
            }

            static bool DecodeYAML(void*, uint32_t version, const SceneYAML::Node& input, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                if (version != 1 || !out || !out->Empty() || out->Remaining() < Codec::PayloadFields)
                {
                    return Reject(diagnostics);
                }
                std::array<SceneValueField, Codec::PayloadFields> shape_fields = {};
                SceneValueWriter                                  shape(shape_fields);
                if (!Codec::Write(typename Codec::Value{}, shape))
                {
                    return Reject(diagnostics);
                }
                auto                  candidate = *out;
                typename Codec::Value value     = {};
                if (!DecodeSceneValueYAML(shape.Value(), input, &candidate, diagnostics) || !Codec::Read(candidate.Value(), value))
                {
                    return Reject(diagnostics);
                }
                *out = candidate;
                return true;
            }
        };

        template <typename Codec>
        bool Register(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics)
        {
            using Fns = Callbacks<Codec>;
            if (const auto* existing = registry.FindByKey(Codec::Key))
            {
                if (existing->Version == 1 && existing->RuntimeType == ComponentTypeOf<typename Codec::Component>() && existing->Codecs.Capture == Fns::Capture && existing->Codecs.PopulateCandidate == Fns::Populate && existing->Codecs.EncodeBinary == Fns::Encode && existing->Codecs.DecodeBinary == Fns::Decode && existing->Codecs.EncodeYAML == Fns::EncodeYAML && existing->Codecs.DecodeYAML == Fns::DecodeYAML)
                {
                    return true;
                }
                SceneDiagnosticError(diagnostics, "An incompatible schema occupies a built-in component key.");
                return false;
            }
            return registry.Register(
                {
                .Key         = Codec::Key,
                .Version     = 1,
                .RuntimeType = ComponentTypeOf<typename Codec::Component>(),
                .Fields      = Codec::Fields.data(),
                .FieldCount  = static_cast<uint32_t>(Codec::Fields.size()),
                .Codecs      = {.Capture = Fns::Capture, .PopulateCandidate = Fns::Populate, .EncodeBinary = Fns::Encode, .DecodeBinary = Fns::Decode, .EncodeYAML = Fns::EncodeYAML, .DecodeYAML = Fns::DecodeYAML, .CandidateMemoryBound = Fns::CandidateMemoryBound},
            },
                diagnostics);
        }
    } // namespace

    bool RegisterBuiltInSceneComponentSchemas(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics)
    {
        return Register<TransformCodec>(registry, diagnostics) && Register<LightCodec>(registry, diagnostics) && Register<CameraCodec>(registry, diagnostics) && RegisterSceneIdentityComponentSchemas(registry, diagnostics);
    }
} // namespace ZEngine::ECS

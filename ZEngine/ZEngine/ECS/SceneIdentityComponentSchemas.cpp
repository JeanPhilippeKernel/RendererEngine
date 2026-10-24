#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>

namespace ZEngine::ECS
{
    namespace
    {
        struct NameCodec
        {
            using Component                         = Components::NameComponent;
            using Value                             = SceneStringValue;
            static constexpr cstring    Key         = "NameComponent";
            static constexpr cstring    Field       = "value";
            static constexpr std::array Fields      = {SceneFieldSchema{Field}};
            static constexpr bool       IsReference = false;

            static bool                 Valid(const Value& value)
            {
                return value.Data && value.Size < sizeof(Component{}.Value) && std::string_view(value.Data, value.Size).find('\0') == std::string_view::npos;
            }
            static bool Capture(EntityID entity, const Scene& scene, Value& out)
            {
                const auto* component = scene.GetComponent<Component>(entity);
                if (!component)
                    return false;
                const auto end = std::find(std::begin(component->Value), std::end(component->Value), '\0');
                if (end == std::end(component->Value))
                    return false;
                out = {component->Value, static_cast<uint32_t>(end - component->Value)};
                return Valid(out);
            }
            static bool Populate(EntityID entity, Scene& scene, const Value& value)
            {
                Component component = {};
                if (Helpers::secure_strncpy(component.Value, sizeof(component.Value), value.Data, value.Size) != Helpers::MEMORY_OP_SUCCESS)
                    return false;
                scene.AddComponent<Component>(entity, component);
                return true;
            }
            static size_t Bytes(const Value& value)
            {
                return sizeof(uint32_t) + value.Size;
            }
            static bool Encode(const Value& value, SceneBinaryWriter& writer)
            {
                return writer.UInt32(value.Size) && writer.Bytes({reinterpret_cast<const uint8_t*>(value.Data), value.Size});
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                uint32_t                 size  = 0;
                std::span<const uint8_t> bytes = {};
                if (!reader.UInt32(size) || size >= sizeof(Component{}.Value) || !reader.Bytes(size, bytes))
                    return false;
                value = {reinterpret_cast<cstring>(bytes.data()), size};
                return Valid(value);
            }
        };

        struct MeshCodec
        {
            using Component                    = Components::MeshComponent;
            using Value                        = uuids::uuid;
            static constexpr cstring    Key    = "MeshComponent";
            static constexpr cstring    Field  = "mesh_uuid";
            static constexpr std::array Fields = {
                SceneFieldSchema{Field},
                SceneFieldSchema{"render_instance_id", SceneFieldClass::RuntimeDerived}
            };
            static constexpr bool               IsReference   = true;
            static constexpr SceneReferenceKind ReferenceKind = SceneReferenceKind::Asset;
            static bool                         Valid(const Value& value)
            {
                return !value.is_nil();
            }
            static bool Capture(EntityID entity, const Scene& scene, Value& out)
            {
                const auto* component = scene.GetComponent<Component>(entity);
                if (!component)
                    return false;
                out = component->MeshUUID;
                return Valid(out);
            }
            static bool Populate(EntityID entity, Scene& scene, const Value& value)
            {
                Component component = {};
                component.MeshUUID  = value;
                scene.AddComponent<Component>(entity, component);
                return true;
            }
            static size_t Bytes(const Value&)
            {
                return 16;
            }
            static bool Encode(const Value& value, SceneBinaryWriter& writer)
            {
                const auto bytes = value.as_bytes();
                return writer.Bytes({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                std::span<const uint8_t> bytes = {};
                if (!reader.Bytes(16, bytes))
                    return false;
                value = uuids::uuid(bytes.begin(), bytes.end());
                return Valid(value);
            }
        };

        struct ParentCodec : MeshCodec
        {
            using Component                                   = Components::ParentComponent;
            static constexpr cstring            Key           = "ParentComponent";
            static constexpr cstring            Field         = "parent_uuid";
            static constexpr std::array         Fields        = {SceneFieldSchema{Field}};
            static constexpr SceneReferenceKind ReferenceKind = SceneReferenceKind::Entity;
            static bool                         Valid(const Value&)
            {
                return true; // null is an explicit root, not a runtime EntityID.
            }
            static bool Capture(EntityID entity, const Scene& scene, Value& out)
            {
                const auto* component = scene.GetComponent<Component>(entity);
                if (!component)
                    return false;
                if (component->Parent == INVALID_ENTITY)
                {
                    out = {};
                    return true;
                }
                const auto* uuid = scene.IsAlive(component->Parent) ? scene.GetComponent<Components::UUIDComponent>(component->Parent) : nullptr;
                if (!uuid || uuid->Value.is_nil())
                    return false;
                out = uuid->Value;
                return true;
            }
            static bool Populate(EntityID, Scene&, const Value&)
            {
                return true; // The second phase owns runtime reference construction.
            }
            static bool Decode(SceneBinaryReader& reader, Value& value)
            {
                std::span<const uint8_t> bytes = {};
                if (!reader.Bytes(16, bytes))
                    return false;
                value = uuids::uuid(bytes.begin(), bytes.end());
                return true;
            }
        };

        template <typename Codec>
        struct Callbacks
        {
            using Value = typename Codec::Value;
            static size_t CandidateMemoryBound(uint32_t count)
            {
                return ComponentStorage<typename Codec::Component>::AllocationUpperBound(count, EntityRegistry::MAX_ENTITIES);
            }
            static bool Reject(SceneDiagnostics* diagnostics)
            {
                SceneDiagnosticError(diagnostics, "Invalid name, mesh asset UUID or parent UUID component payload.");
                return false;
            }
            static bool Read(const SceneValue& payload, Value& value)
            {
                const auto* field = payload.Find<Value>(Codec::Field);
                if (payload.Fields().size() != 1 || !field || !Codec::Valid(*field))
                    return false;
                value = *field;
                return true;
            }
            static bool Write(const Value& value, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                return out && out->Empty() && out->Remaining() && Codec::Valid(value) && out->Add(Codec::Field, value) ? true : Reject(diagnostics);
            }
            static bool Capture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                Value value = {};
                return scene.IsAlive(entity) && Codec::Capture(entity, scene, value) ? Write(value, out, diagnostics) : Reject(diagnostics);
            }
            static bool Populate(void*, EntityID entity, Scene& scene, const SceneValue& payload, SceneDiagnostics* diagnostics)
            {
                Value value = {};
                return scene.IsAlive(entity) && !scene.HasComponent<typename Codec::Component>(entity) && Read(payload, value) && Codec::Populate(entity, scene, value) ? true : Reject(diagnostics);
            }
            static bool Encode(void*, const SceneValue& payload, SceneBinaryWriter* out, SceneDiagnostics* diagnostics)
            {
                Value value = {};
                if (!out || !Read(payload, value) || out->Remaining() < Codec::Bytes(value))
                    return Reject(diagnostics);
                auto candidate = *out;
                if (!Codec::Encode(value, candidate))
                    return Reject(diagnostics);
                *out = candidate;
                return true;
            }
            static bool Decode(void*, uint32_t version, SceneBinaryReader* input, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                if (version != 1 || !input)
                    return Reject(diagnostics);
                auto  candidate = *input;
                Value value     = {};
                if (!Codec::Decode(candidate, value) || candidate.Remaining() || !Write(value, out, diagnostics))
                    return Reject(diagnostics);
                *input = candidate;
                return true;
            }
            static bool EncodeYAML(void*, const SceneValue& payload, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
            {
                Value value = {};
                return Read(payload, value) ? EncodeSceneValueYAML(payload, out, diagnostics) : Reject(diagnostics);
            }
            static bool DecodeYAML(void*, uint32_t version, const SceneYAML::Node& input, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                if (version != 1 || !out || !out->Empty() || !out->Remaining())
                    return Reject(diagnostics);
                const std::array shape = {
                    SceneValueField{Codec::Field, Value{}}
                };
                auto  candidate = *out;
                Value value     = {};
                if (!DecodeSceneValueYAML(SceneValue(shape), input, &candidate, diagnostics) || !Read(candidate.Value(), value))
                    return Reject(diagnostics);
                *out = candidate;
                return true;
            }
            static bool Enumerate(void*, const SceneValue& payload, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics)
            {
                if constexpr (Codec::IsReference)
                {
                    Value value = {};
                    if (!visitor || !Read(payload, value))
                        return Reject(diagnostics);
                    return value.is_nil() || visitor(context, {Codec::ReferenceKind, value});
                }
                return Reject(diagnostics);
            }
            static bool Remap(void*, const SceneValue& payload, const SceneUUIDRemap& remap, SceneValueWriter* out, SceneDiagnostics* diagnostics)
            {
                if constexpr (Codec::IsReference)
                {
                    Value value = {};
                    if (!Read(payload, value) || (!value.is_nil() && (!remap.Find || !remap.Find(remap.Context, value, &value))))
                        return Reject(diagnostics);
                    return Write(value, out, diagnostics);
                }
                return Reject(diagnostics);
            }
            static bool Resolve(void*, EntityID entity, Scene& scene, const SceneValue& payload, const SceneEntityResolver& resolver, SceneDiagnostics* diagnostics)
            {
                if constexpr (std::is_same_v<Codec, ParentCodec>)
                {
                    Value value = {};
                    if (!scene.IsAlive(entity) || scene.HasComponent<Components::ParentComponent>(entity) || !Read(payload, value))
                        return Reject(diagnostics);
                    const auto parent = value.is_nil() ? INVALID_ENTITY : (resolver.Find ? resolver.Find(resolver.Context, value) : INVALID_ENTITY);
                    if (!value.is_nil() && !scene.IsAlive(parent))
                        return Reject(diagnostics);
                    // Whole-graph cycle validation follows all staged reference writes, O(V+E).
                    scene.AddComponent<Components::ParentComponent>(entity, {parent});
                    return true;
                }
                return Reject(diagnostics);
            }
        };

        template <typename Codec>
        bool Register(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics)
        {
            using Fns = Callbacks<Codec>;
            if (const auto* existing = registry.FindByKey(Codec::Key))
            {
                if (existing->Version == 1 && existing->RuntimeType == ComponentTypeOf<typename Codec::Component>() && existing->Codecs.Capture == Fns::Capture && existing->Codecs.PopulateCandidate == Fns::Populate && existing->Codecs.EncodeBinary == Fns::Encode && existing->Codecs.DecodeBinary == Fns::Decode && existing->Codecs.EncodeYAML == Fns::EncodeYAML && existing->Codecs.DecodeYAML == Fns::DecodeYAML)
                    return true;
                return Fns::Reject(diagnostics);
            }
            SceneComponentSchemaDesc desc = {};
            desc.Key                      = Codec::Key;
            desc.Version                  = 1;
            desc.RuntimeType              = ComponentTypeOf<typename Codec::Component>();
            desc.Fields                   = Codec::Fields.data();
            desc.FieldCount               = static_cast<uint32_t>(Codec::Fields.size());
            desc.Codecs                   = {.Capture = Fns::Capture, .PopulateCandidate = Fns::Populate, .EncodeBinary = Fns::Encode, .DecodeBinary = Fns::Decode, .EncodeYAML = Fns::EncodeYAML, .DecodeYAML = Fns::DecodeYAML, .CandidateMemoryBound = Fns::CandidateMemoryBound};
            if constexpr (Codec::IsReference)
                desc.References = {.Enumerate = Fns::Enumerate, .Remap = Fns::Remap};
            if constexpr (std::is_same_v<Codec, ParentCodec>)
                desc.Codecs.ResolveCandidateReferences = Fns::Resolve;
            return registry.Register(desc, diagnostics);
        }
    } // namespace

    bool RegisterSceneIdentityComponentSchemas(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics)
    {
        return Register<NameCodec>(registry, diagnostics) && Register<MeshCodec>(registry, diagnostics) && Register<ParentCodec>(registry, diagnostics);
    }
} // namespace ZEngine::ECS

#include "SceneCookCustomSchema.h"
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <cmath>

namespace ZEngine::Cook::Tests
{
    namespace
    {
        constexpr cstring                                                TextureField = "texture_uuid";
        constexpr cstring                                                WeightField  = "weight";
        constexpr Core::Containers::FixedArray<ECS::SceneFieldSchema, 3> Fields       = {
            {{TextureField}, {WeightField}, {"runtime_cache", ECS::SceneFieldClass::RuntimeDerived}}
        };

        bool Reject(ECS::SceneDiagnostics* diagnostics)
        {
            ECS::SceneDiagnosticError(diagnostics, "Invalid test GameSurface component payload.");
            return false;
        }

        bool Valid(const CookCustomComponent& value)
        {
            return !value.Texture.is_nil() && std::isfinite(value.Weight) && value.Weight >= 0 && value.Weight <= 1;
        }

        bool Read(const ECS::SceneValue& payload, CookCustomComponent* value)
        {
            const auto* texture = payload.Find<uuids::uuid>(TextureField);
            const auto* weight  = payload.Find<float>(WeightField);
            if (payload.Fields().size() != 2 || !texture || !weight)
                return false;
            *value = {.Texture = *texture, .Weight = *weight};
            return Valid(*value);
        }

        bool Write(const CookCustomComponent& value, ECS::SceneValueWriter* output)
        {
            if (!output || !output->Empty() || output->Remaining() < 2 || !Valid(value))
                return false;
            auto candidate = *output;
            if (!candidate.Add(TextureField, value.Texture) || !candidate.Add(WeightField, value.Weight))
                return false;
            *output = candidate;
            return true;
        }

        template <uint32_t Version>
        struct Codecs
        {
            static bool Capture(void*, ECS::EntityID entity, const ECS::Scene& scene, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics)
            {
                const auto* component = scene.GetComponent<CookCustomComponent>(entity);
                return (component && Write(*component, output)) || Reject(diagnostics);
            }

            static bool Populate(void*, ECS::EntityID entity, ECS::Scene& scene, const ECS::SceneValue& payload, ECS::SceneDiagnostics* diagnostics)
            {
                CookCustomComponent value = {};
                if (!Read(payload, &value))
                    return Reject(diagnostics);
                scene.AddComponent<CookCustomComponent>(entity, value);
                return true;
            }

            static bool Encode(void*, const ECS::SceneValue& payload, ECS::SceneBinaryWriter* output, ECS::SceneDiagnostics* diagnostics)
            {
                CookCustomComponent value = {};
                if (!output || !Read(payload, &value) || !output->Require(20))
                    return Reject(diagnostics);
                auto       candidate = *output;
                const auto bytes     = value.Texture.as_bytes();
                if (!candidate.Bytes({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}) || !candidate.Float(value.Weight))
                    return Reject(diagnostics);
                *output = candidate;
                return true;
            }

            static bool Decode(void*, uint32_t version, ECS::SceneBinaryReader* input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics)
            {
                if (version != Version || !input)
                    return Reject(diagnostics);
                auto                     candidate  = *input;
                CookCustomComponent      value      = {};
                std::span<const uint8_t> uuid_bytes = {};
                if (!candidate.Bytes(16, uuid_bytes) || !candidate.Float(value.Weight) || candidate.Remaining())
                    return Reject(diagnostics);
                value.Texture = uuids::uuid(uuid_bytes.begin(), uuid_bytes.end());
                if (!Write(value, output))
                    return Reject(diagnostics);
                *input = candidate;
                return true;
            }

            static bool EncodeYAML(void*, const ECS::SceneValue& payload, ECS::SceneYAML::Node* output, ECS::SceneDiagnostics* diagnostics)
            {
                CookCustomComponent value = {};
                return Read(payload, &value) ? ECS::EncodeSceneValueYAML(payload, output, diagnostics) : Reject(diagnostics);
            }

            static bool DecodeYAML(void*, uint32_t version, const ECS::SceneYAML::Node& input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics)
            {
                if (version != Version || !output || !output->Empty() || output->Remaining() < 2)
                    return Reject(diagnostics);
                const Core::Containers::FixedArray<ECS::SceneValueField, 2> shape = {
                    {{TextureField, uuids::uuid{}}, {WeightField, 0.f}}
                };
                auto                candidate = *output;
                CookCustomComponent value     = {};
                if (!ECS::DecodeSceneValueYAML(ECS::SceneValue({shape.data(), shape.size()}), input, &candidate, diagnostics) || !Read(candidate.Value(), &value))
                    return Reject(diagnostics);
                *output = candidate;
                return true;
            }

            static bool Enumerate(void*, const ECS::SceneValue& payload, ECS::SceneReferenceVisitorFn visitor, void* context, ECS::SceneDiagnostics* diagnostics)
            {
                CookCustomComponent value = {};
                if (!visitor || !Read(payload, &value))
                    return Reject(diagnostics);
                return visitor(context, {ECS::SceneReferenceKind::Asset, value.Texture, TextureField, "texture"});
            }

            static bool Remap(void*, const ECS::SceneValue& payload, const ECS::SceneUUIDRemap& remap, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics)
            {
                CookCustomComponent value = {};
                return (Read(payload, &value) && remap.Find && remap.Find(remap.Context, value.Texture, &value.Texture) && Write(value, output)) || Reject(diagnostics);
            }

            static size_t CandidateMemoryBound(uint32_t count)
            {
                return ECS::ComponentStorage<CookCustomComponent>::AllocationUpperBound(count, ECS::EntityRegistry::MAX_ENTITIES);
            }

            static bool Register(ECS::SceneComponentSchemaRegistry& schemas, ECS::SceneDiagnostics* diagnostics)
            {
                return schemas.Register(
                    {
                    .Key = CookCustomSchemaKey, .Version = Version, .RuntimeType = ECS::ComponentTypeOf<CookCustomComponent>(), .Fields = Fields.data(), .FieldCount = static_cast<uint32_t>(Fields.size()), .Codecs = {.Capture = Capture, .PopulateCandidate = Populate, .EncodeBinary = Encode, .DecodeBinary = Decode, .EncodeYAML = EncodeYAML, .DecodeYAML = DecodeYAML, .CandidateMemoryBound = CandidateMemoryBound},
                                     .References = {.Enumerate = Enumerate, .Remap = Remap}
                },
                    diagnostics);
            }
        };
    } // namespace

    bool RegisterCookCustomSchema(ECS::SceneComponentSchemaRegistry& schemas, uint32_t version, ECS::SceneDiagnostics* diagnostics)
    {
        return version == 1 ? Codecs<1>::Register(schemas, diagnostics) : version == 2 ? Codecs<2>::Register(schemas, diagnostics) : Reject(diagnostics);
    }

    bool MigrateCookCustomSchema(void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* output, ECS::SceneDiagnostics*)
    {
        if (!output || !output->Owner())
            return false;
        // The v2 wire version is an explicit recook boundary; authored field
        // semantics are unchanged. Source migration uses the cook-routed owner.
        *output = output->Owner()->Clone(source);
        return output->IsDefined();
    }
} // namespace ZEngine::Cook::Tests

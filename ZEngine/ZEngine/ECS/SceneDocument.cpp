#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/EntityRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace ZEngine::ECS
{
    namespace
    {
        constexpr float ORIENTATION_TOLERANCE = 1e-3f;

        void            Failf(SceneDiagnostics* diagnostics, cstring format, ...)
        {
            char    message[SceneDiagnostics::MAX_MESSAGE_LEN];
            va_list args;
            va_start(args, format);
            vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            SceneDiagnosticError(diagnostics, message);
        }

        bool IsHex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        bool IsFinite(const float* values, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!Core::Maths::isfinite(values[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsUnitColor(const Core::Maths::Vec4f& c)
        {
            const float values[4] = {c.x, c.y, c.z, c.w};
            for (float v : values)
            {
                if (!Core::Maths::isfinite(v) || v < 0.0f || v > 1.0f)
                {
                    return false;
                }
            }
            return true;
        }

        struct ValueCheck
        {
            const SceneSerializationLimits& Limits;
            SceneDiagnostics*               Diagnostics;
            cstring                         Where;
        };

        bool ValidateValue(const ValueCheck& check, const SceneValue* value, uint32_t depth)
        {
            if (!value)
            {
                Failf(check.Diagnostics, "%s: null value node", check.Where);
                return false;
            }
            if (depth > check.Limits.MaxNestingDepth)
            {
                Failf(check.Diagnostics, "%s: nesting depth exceeds %u", check.Where, check.Limits.MaxNestingDepth);
                return false;
            }

            switch (value->Kind)
            {
                case SceneValueKind::Null:
                case SceneValueKind::Bool:
                case SceneValueKind::SInt:
                case SceneValueKind::UInt:
                    return true;
                case SceneValueKind::Float:
                    if (!Core::Maths::isfinite(value->FloatValue))
                    {
                        Failf(check.Diagnostics, "%s: non-finite float", check.Where);
                        return false;
                    }
                    return true;
                case SceneValueKind::String:
                    if (!value->StringValue)
                    {
                        Failf(check.Diagnostics, "%s: string value is null", check.Where);
                        return false;
                    }
                    if (Helpers::secure_strlen(value->StringValue) > check.Limits.MaxStringBytes)
                    {
                        Failf(check.Diagnostics, "%s: string exceeds %u bytes", check.Where, check.Limits.MaxStringBytes);
                        return false;
                    }
                    return true;
                case SceneValueKind::Sequence:
                {
                    if (value->Sequence.size() > check.Limits.MaxCollectionEntries)
                    {
                        Failf(check.Diagnostics, "%s: sequence exceeds %u entries", check.Where, check.Limits.MaxCollectionEntries);
                        return false;
                    }
                    for (uint32_t i = 0; i < value->Sequence.size(); ++i)
                    {
                        if (!ValidateValue(check, value->Sequence[i], depth + 1))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                case SceneValueKind::Map:
                {
                    const uint32_t count = static_cast<uint32_t>(value->Members.size());
                    if (count > check.Limits.MaxCollectionEntries)
                    {
                        Failf(check.Diagnostics, "%s: map exceeds %u entries", check.Where, check.Limits.MaxCollectionEntries);
                        return false;
                    }
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        cstring key = value->Members[i].Key;
                        if (!SceneComponentSchemaRegistry::IsValidKey(key))
                        {
                            Failf(check.Diagnostics, "%s: map key is empty, too long, or has reserved characters", check.Where);
                            return false;
                        }
                        for (uint32_t j = 0; j < i; ++j)
                        {
                            if (Helpers::secure_strcmp(key, value->Members[j].Key) == 0)
                            {
                                Failf(check.Diagnostics, "%s: duplicate map key '%s'", check.Where, key);
                                return false;
                            }
                        }
                        if (!ValidateValue(check, value->Members[i].Value, depth + 1))
                        {
                            return false;
                        }
                    }
                    return true;
                }
            }

            Failf(check.Diagnostics, "%s: illegal value kind %u", check.Where, static_cast<uint32_t>(value->Kind));
            return false;
        }

        bool ContainsEntity(const SceneDocument& document, const uuids::uuid& uuid)
        {
            // Entities are strictly ascending by UUID once structural validation passed.
            size_t lo = 0;
            size_t hi = document.Entities.size();
            while (lo < hi)
            {
                const size_t mid = lo + (hi - lo) / 2;
                if (document.Entities[mid].UUID < uuid)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            return lo < document.Entities.size() && document.Entities[lo].UUID == uuid;
        }

        struct ReferenceCheck
        {
            const SceneDocument* Document;
            SceneDiagnostics*    Diagnostics;
            cstring              Where;
            bool                 Ok;
        };

        bool CheckReference(void* context, const SceneReference& reference)
        {
            auto* check = static_cast<ReferenceCheck*>(context);
            if (reference.UUID.is_nil())
            {
                Failf(check->Diagnostics, "%s: reference has a nil UUID", check->Where);
                check->Ok = false;
            }
            else if (reference.Kind == SceneReferenceKind::Entity && !ContainsEntity(*check->Document, reference.UUID))
            {
                char text[SCENE_UUID_STRING_LEN + 1];
                SceneUUIDToString(reference.UUID, text);
                Failf(check->Diagnostics, "%s: entity reference %s is not in the document", check->Where, text);
                check->Ok = false;
            }
            else if (reference.Kind != SceneReferenceKind::Entity && reference.Kind != SceneReferenceKind::Asset)
            {
                Failf(check->Diagnostics, "%s: illegal reference kind", check->Where);
                check->Ok = false;
            }
            return true;
        }

        bool ValidateAgainstSchema(const SceneDocument& document, const SceneComponentRecord& record, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics, cstring where)
        {
            const SceneComponentSchema* schema = schemas.FindByKey(record.SchemaKey);
            if (!schema)
            {
                if (!record.IsOpaque)
                {
                    Failf(diagnostics, "%s: unknown component '%s' must be preserved as opaque or rejected", where, record.SchemaKey);
                    return false;
                }
                return true;
            }

            if (record.IsOpaque)
            {
                Failf(diagnostics, "%s: registered component '%s' is marked opaque", where, record.SchemaKey);
                return false;
            }
            if (schema->RuntimeType == ComponentTypeOf<Components::UUIDComponent>())
            {
                Failf(diagnostics, "%s: entity UUID is the record identity and must not repeat as '%s'", where, record.SchemaKey);
                return false;
            }
            if (record.Version != schema->Version)
            {
                Failf(diagnostics, "%s: '%s' version %u does not match registered version %u", where, record.SchemaKey, record.Version, schema->Version);
                return false;
            }

            bool ok = true;
            if (schema->FieldCount > 0)
            {
                if (record.Payload->Kind != SceneValueKind::Map)
                {
                    Failf(diagnostics, "%s: '%s' payload must be a map", where, record.SchemaKey);
                    return false;
                }
                for (uint32_t i = 0; i < record.Payload->Members.size(); ++i)
                {
                    cstring                 key   = record.Payload->Members[i].Key;
                    const SceneFieldSchema* field = nullptr;
                    for (uint32_t f = 0; f < schema->FieldCount; ++f)
                    {
                        if (Helpers::secure_strcmp(schema->Fields[f].Key, key) == 0)
                        {
                            field = &schema->Fields[f];
                            break;
                        }
                    }
                    if (!field)
                    {
                        Failf(diagnostics, "%s: '%s' has undeclared field '%s'", where, record.SchemaKey, key);
                        ok = false;
                    }
                    else if (field->Class == SceneFieldClass::RuntimeDerived || field->Class == SceneFieldClass::Forbidden)
                    {
                        Failf(diagnostics, "%s: runtime-only field '%s.%s' cannot enter source data", where, record.SchemaKey, key);
                        ok = false;
                    }
                }
            }

            if (ok && schema->References.Enumerate)
            {
                ReferenceCheck check{&document, diagnostics, where, true};
                if (!schema->References.Enumerate(schema->References.Context, *record.Payload, CheckReference, &check, diagnostics))
                {
                    Failf(diagnostics, "%s: '%s' reference enumeration failed", where, record.SchemaKey);
                    return false;
                }
                ok = check.Ok;
            }
            return ok;
        }
    } // namespace

    void SceneDocument::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t entity_capacity)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "SceneDocument::Initialize: arena must not be null")
        SchemaVersion      = SCENE_DOCUMENT_SCHEMA_VERSION;
        CompatibleFeatures = 0;
        UUID               = {};
        Grid               = {};
        Name.init(arena, "");
        Entities.init(arena, entity_capacity ? entity_capacity : 1);
    }

    SceneSerializationLimits SceneSerializationLimits::Default()
    {
        SceneSerializationLimits limits{};
        limits.MaxSourceBytes         = 64ull * 1024ull * 1024ull;
        limits.MaxEntities            = EntityRegistry::MAX_ENTITIES;
        limits.MaxComponentsPerEntity = ARCHETYPE_MASK_CAPACITY;
        limits.MaxStringBytes         = 4096;
        limits.MaxCollectionEntries   = 16384;
        limits.MaxNestingDepth        = 32;
        return limits;
    }

    bool SceneUUIDFromString(cstring text, uuids::uuid* out_uuid)
    {
        if (!text || !out_uuid || Helpers::secure_strlen(text) != SCENE_UUID_STRING_LEN)
        {
            return false;
        }
        for (uint32_t i = 0; i < SCENE_UUID_STRING_LEN; ++i)
        {
            const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
            if (dash ? text[i] != '-' : !IsHex(text[i]))
            {
                return false;
            }
        }
        auto parsed = uuids::uuid::from_string(text);
        if (!parsed.has_value())
        {
            return false;
        }
        *out_uuid = parsed.value();
        return true;
    }

    void SceneUUIDToString(const uuids::uuid& uuid, char* out)
    {
        uuids::to_string<char>(uuid, out);
        out[SCENE_UUID_STRING_LEN] = '\0';
    }

    bool CanonicalizeSceneGridSettings(SceneGridSettings& grid, SceneDiagnostics* diagnostics)
    {
        bool        ok        = true;
        const float origin[3] = {grid.Origin.x, grid.Origin.y, grid.Origin.z};
        if (!IsFinite(origin, 3))
        {
            Failf(diagnostics, "grid: origin must be finite");
            ok = false;
        }

        auto&       q      = grid.Orientation;
        const float rot[4] = {q.x, q.y, q.z, q.w};
        const float len    = IsFinite(rot, 4) ? Core::Maths::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w) : 0.0f;
        if (!Core::Maths::isfinite(len) || Core::Maths::abs(len - 1.0f) > ORIENTATION_TOLERANCE)
        {
            Failf(diagnostics, "grid: orientation must be a finite unit quaternion");
            ok = false;
        }
        else
        {
            // q and -q are the same rotation; w >= 0 keeps one spelling.
            const float sign = q.w < 0.0f ? -1.0f : 1.0f;
            q                = Core::Maths::Quatf(sign * q.x / len, sign * q.y / len, sign * q.z / len, sign * q.w / len);
        }

        if (!Core::Maths::isfinite(grid.BaseSpacing) || grid.BaseSpacing <= 0.0f)
        {
            Failf(diagnostics, "grid: base_spacing must be finite and > 0");
            ok = false;
        }
        if (grid.MajorLineEvery == 0)
        {
            Failf(diagnostics, "grid: major_line_every must be > 0");
            ok = false;
        }
        if (!IsUnitColor(grid.MinorColor) || !IsUnitColor(grid.MajorColor) || !IsUnitColor(grid.UAxisColor) || !IsUnitColor(grid.VAxisColor))
        {
            Failf(diagnostics, "grid: colours must be finite and within [0, 1]");
            ok = false;
        }
        if (!Core::Maths::isfinite(grid.FadeDistance) || grid.FadeDistance < 0.0f)
        {
            Failf(diagnostics, "grid: fade_distance must be finite and >= 0");
            ok = false;
        }
        if (!Core::Maths::isfinite(grid.FadeStrength) || grid.FadeStrength < 0.0f || grid.FadeStrength > 1.0f)
        {
            Failf(diagnostics, "grid: fade_strength must be finite and within [0, 1]");
            ok = false;
        }
        if (!Core::Maths::isfinite(grid.LineWidth) || grid.LineWidth <= 0.0f)
        {
            Failf(diagnostics, "grid: line_width must be finite and > 0");
            ok = false;
        }
        if (grid.MaxLOD > SceneGridSettings::MAX_LOD)
        {
            Failf(diagnostics, "grid: max_lod must be <= %u", SceneGridSettings::MAX_LOD);
            ok = false;
        }
        return ok;
    }

    void SortSceneDocument(SceneDocument& document)
    {
        for (auto& entity : document.Entities)
        {
            std::sort(entity.Components.begin(), entity.Components.end(), [](const SceneComponentRecord& a, const SceneComponentRecord& b) { return Helpers::secure_strcmp(a.SchemaKey ? a.SchemaKey : "", b.SchemaKey ? b.SchemaKey : "") < 0; });
        }
        std::sort(document.Entities.begin(), document.Entities.end(), [](const SceneEntityRecord& a, const SceneEntityRecord& b) { return a.UUID < b.UUID; });
    }

    bool ValidateSceneDocument(const SceneDocument& document, const SceneComponentSchemaRegistry* schemas, const SceneSerializationLimits& limits, SceneDiagnostics* diagnostics)
    {
        bool ok = true;

        if (document.SchemaVersion != SCENE_DOCUMENT_SCHEMA_VERSION)
        {
            Failf(diagnostics, "scene: schema_version %u is not supported (expected %u)", document.SchemaVersion, SCENE_DOCUMENT_SCHEMA_VERSION);
            ok = false;
        }
        if (document.UUID.is_nil())
        {
            Failf(diagnostics, "scene: uuid must not be nil");
            ok = false;
        }
        const size_t name_len = document.Name.c_str() ? Helpers::secure_strlen(document.Name.c_str()) : 0;
        if (name_len > limits.MaxStringBytes || name_len >= Scene::MAX_NAME_LEN)
        {
            Failf(diagnostics, "scene: name is longer than %u bytes", Scene::MAX_NAME_LEN - 1);
            ok = false;
        }

        SceneGridSettings grid = document.Grid;
        ok                     = CanonicalizeSceneGridSettings(grid, diagnostics) && ok;

        if (document.Entities.size() > limits.MaxEntities)
        {
            Failf(diagnostics, "scene: %u entities exceed the limit of %u", static_cast<uint32_t>(document.Entities.size()), limits.MaxEntities);
            return false;
        }

        // Pass 1: identities and structure. Reference checks need the sorted UUID set.
        bool structure_ok = true;
        for (uint32_t e = 0; e < document.Entities.size(); ++e)
        {
            const SceneEntityRecord& entity = document.Entities[e];
            char                     where[64];
            char                     text[SCENE_UUID_STRING_LEN + 1];
            SceneUUIDToString(entity.UUID, text);
            snprintf(where, sizeof(where), "entity %s", text);

            if (entity.UUID.is_nil())
            {
                Failf(diagnostics, "entity #%u: uuid must not be nil", e);
                structure_ok = false;
            }
            else if (e > 0 && !(document.Entities[e - 1].UUID < entity.UUID))
            {
                Failf(diagnostics, "%s: duplicate or out-of-order entity uuid", where);
                structure_ok = false;
            }

            if (entity.Components.size() > limits.MaxComponentsPerEntity)
            {
                Failf(diagnostics, "%s: %u components exceed the limit of %u", where, static_cast<uint32_t>(entity.Components.size()), limits.MaxComponentsPerEntity);
                structure_ok = false;
                continue;
            }

            for (uint32_t c = 0; c < entity.Components.size(); ++c)
            {
                const SceneComponentRecord& record = entity.Components[c];
                if (!SceneComponentSchemaRegistry::IsValidKey(record.SchemaKey))
                {
                    Failf(diagnostics, "%s: component key is empty, too long, or has reserved characters", where);
                    structure_ok = false;
                    continue;
                }
                if (c > 0 && Helpers::secure_strcmp(entity.Components[c - 1].SchemaKey, record.SchemaKey) >= 0)
                {
                    Failf(diagnostics, "%s: duplicate or out-of-order component '%s'", where, record.SchemaKey);
                    structure_ok = false;
                }
                if (record.Version == 0)
                {
                    Failf(diagnostics, "%s: '%s' version 0 is reserved", where, record.SchemaKey);
                    structure_ok = false;
                }
                ValueCheck check{limits, diagnostics, where};
                structure_ok = ValidateValue(check, record.Payload, 1) && structure_ok;
            }
        }
        ok = ok && structure_ok;

        if (!structure_ok || !schemas)
        {
            return ok;
        }

        for (uint32_t e = 0; e < document.Entities.size(); ++e)
        {
            const SceneEntityRecord& entity = document.Entities[e];
            char                     where[64];
            char                     text[SCENE_UUID_STRING_LEN + 1];
            SceneUUIDToString(entity.UUID, text);
            snprintf(where, sizeof(where), "entity %s", text);
            for (uint32_t c = 0; c < entity.Components.size(); ++c)
            {
                ok = ValidateAgainstSchema(document, entity.Components[c], *schemas, diagnostics, where) && ok;
            }
        }
        return ok;
    }
} // namespace ZEngine::ECS

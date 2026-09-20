#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    namespace
    {
        constexpr uint32_t MAX_KEY_LEN = 128;

        bool               IsValidKey(cstring key)
        {
            if (!key || key[0] == '\0')
            {
                return false;
            }

            const size_t len = Helpers::secure_strlen(key);
            if (len >= MAX_KEY_LEN)
            {
                return false;
            }

            // Wire keys stay printable and separator-free so they survive any format.
            for (size_t i = 0; i < len; ++i)
            {
                const char c = key[i];
                if (c <= ' ' || c == 0x7F || c == '/' || c == '\\' || c == '"')
                {
                    return false;
                }
            }
            return true;
        }

        void Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            ZENGINE_CORE_ERROR("SceneComponentSchemaRegistry::Register rejected: {}", message)
        }
    } // namespace

    bool SceneComponentSchemaRegistry::IsInitialized() const
    {
        return m_arena != nullptr;
    }

    void SceneComponentSchemaRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "SceneComponentSchemaRegistry::Initialize: arena must not be null")
        ZENGINE_VALIDATE_ASSERT(capacity > 0, "SceneComponentSchemaRegistry::Initialize: capacity must be > 0")

        if (m_arena)
        {
            return;
        }

        m_arena = arena;
        m_schemas.init(arena, capacity);
        m_canonical.init(arena, capacity);
    }

    cstring SceneComponentSchemaRegistry::CopyKey(cstring key)
    {
        const size_t len  = Helpers::secure_strlen(key);
        char*        copy = ZPushString(m_arena, len + 1);
        Helpers::secure_strncpy(copy, len + 1, key, len);
        return copy;
    }

    bool SceneComponentSchemaRegistry::Register(const SceneComponentSchemaDesc& desc, SceneDiagnostics* diagnostics)
    {
        if (!IsInitialized())
        {
            Reject(diagnostics, "registry is not initialized");
            return false;
        }

        if (!IsValidKey(desc.Key))
        {
            Reject(diagnostics, "schema key is null, empty, too long, or contains reserved characters");
            return false;
        }

        if (desc.Version == 0)
        {
            Reject(diagnostics, "schema version 0 is reserved");
            return false;
        }

        if (FindByKey(desc.Key))
        {
            Reject(diagnostics, "schema key is already registered");
            return false;
        }

        if (FindByRuntimeType(desc.RuntimeType))
        {
            Reject(diagnostics, "runtime component type is already registered under another key");
            return false;
        }

        if (desc.FieldCount > 0 && !desc.Fields)
        {
            Reject(diagnostics, "FieldCount > 0 but Fields is null");
            return false;
        }

        for (uint32_t i = 0; i < desc.FieldCount; ++i)
        {
            if (!IsValidKey(desc.Fields[i].Key))
            {
                Reject(diagnostics, "field key is null, empty, too long, or contains reserved characters");
                return false;
            }
            for (uint32_t j = 0; j < i; ++j)
            {
                if (Helpers::secure_strcmp(desc.Fields[i].Key, desc.Fields[j].Key) == 0)
                {
                    Reject(diagnostics, "duplicate field key within one schema");
                    return false;
                }
            }
        }

        const auto& codecs = desc.Codecs;
        if (!codecs.Capture || !codecs.PopulateCandidate || !codecs.EncodeBinary || !codecs.DecodeBinary)
        {
            Reject(diagnostics, "all four codec callbacks are required");
            return false;
        }

        if (static_cast<bool>(desc.References.Enumerate) != static_cast<bool>(desc.References.Remap))
        {
            Reject(diagnostics, "reference hooks must provide both Enumerate and Remap, or neither");
            return false;
        }

        if (m_schemas.size() >= m_schemas.capacity())
        {
            Reject(diagnostics, "exceeded the capacity reserved by Initialize()");
            return false;
        }

        SceneComponentSchema schema{};
        schema.Key         = CopyKey(desc.Key);
        schema.Version     = desc.Version;
        schema.RuntimeType = desc.RuntimeType;
        schema.FieldCount  = desc.FieldCount;
        schema.Codecs      = desc.Codecs;
        schema.References  = desc.References;

        if (desc.FieldCount > 0)
        {
            auto* fields = ZPushArray(m_arena, SceneFieldSchema, desc.FieldCount);
            for (uint32_t i = 0; i < desc.FieldCount; ++i)
            {
                fields[i].Key   = CopyKey(desc.Fields[i].Key);
                fields[i].Class = desc.Fields[i].Class;
            }
            schema.Fields = fields;
        }

        m_schemas.push(schema);

        // Keep m_canonical sorted so ForEachCanonical never depends on call order.
        const uint32_t new_index = static_cast<uint32_t>(m_schemas.size()) - 1;
        uint32_t       insert_at = static_cast<uint32_t>(m_canonical.size());
        for (uint32_t i = 0; i < m_canonical.size(); ++i)
        {
            if (Helpers::secure_strcmp(schema.Key, m_schemas[m_canonical[i]].Key) < 0)
            {
                insert_at = i;
                break;
            }
        }
        m_canonical.insert(insert_at, new_index);

        return true;
    }

    const SceneComponentSchema* SceneComponentSchemaRegistry::FindByKey(cstring key) const
    {
        if (!key)
        {
            return nullptr;
        }

        for (uint32_t i = 0; i < m_schemas.size(); ++i)
        {
            if (Helpers::secure_strcmp(m_schemas[i].Key, key) == 0)
            {
                return &m_schemas[i];
            }
        }
        return nullptr;
    }

    const SceneComponentSchema* SceneComponentSchemaRegistry::FindByRuntimeType(ComponentTypeID type) const
    {
        for (uint32_t i = 0; i < m_schemas.size(); ++i)
        {
            if (m_schemas[i].RuntimeType == type)
            {
                return &m_schemas[i];
            }
        }
        return nullptr;
    }

    void SceneComponentSchemaRegistry::ForEachCanonical(SceneSchemaVisitorFn visitor, void* ctx) const
    {
        if (!visitor)
        {
            return;
        }

        for (uint32_t i = 0; i < m_canonical.size(); ++i)
        {
            visitor(ctx, m_schemas[m_canonical[i]]);
        }
    }

    uint32_t SceneComponentSchemaRegistry::Count() const
    {
        return static_cast<uint32_t>(m_schemas.size());
    }
} // namespace ZEngine::ECS

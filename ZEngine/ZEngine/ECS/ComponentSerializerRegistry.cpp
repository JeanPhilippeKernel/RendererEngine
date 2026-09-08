#include <ZEngine/ECS/ComponentSerializerRegistry.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    ComponentSerializerRegistry& ComponentSerializerRegistry::Get()
    {
        static ComponentSerializerRegistry s_instance;
        return s_instance;
    }

    bool ComponentSerializerRegistry::IsInitialized() const
    {
        return m_arena != nullptr;
    }

    void ComponentSerializerRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "ComponentSerializerRegistry::Initialize: arena must not be null")
        ZENGINE_VALIDATE_ASSERT(capacity > 0, "ComponentSerializerRegistry::Initialize: capacity must be > 0")

        if (m_arena)
        {
            return;
        }

        m_arena = arena;
        m_ids.init(arena, capacity);
        m_fns.init(arena, capacity);
    }

    void ComponentSerializerRegistry::Register(ComponentTypeID type_id, ComponentSerializeFns fns)
    {
        ZENGINE_VALIDATE_ASSERT(m_arena != nullptr, "ComponentSerializerRegistry::Register: Initialize() must be called first")

        if (Lookup(type_id))
        {
            ZENGINE_CORE_WARN("ComponentSerializerRegistry: TypeID {} already registered, ignoring", type_id);
            return;
        }

        ZENGINE_VALIDATE_ASSERT(m_ids.size() < m_ids.capacity(), "ComponentSerializerRegistry::Register: exceeded the capacity reserved by Initialize() — pass a larger capacity there")

        m_ids.push(type_id);
        m_fns.push(fns);
    }

    const ComponentSerializeFns* ComponentSerializerRegistry::Lookup(ComponentTypeID type_id) const
    {
        for (uint32_t i = 0; i < m_ids.size(); ++i)
        {
            if (m_ids[i] == type_id)
            {
                return &m_fns[i];
            }
        }
        return nullptr;
    }

    void ComponentSerializerRegistry::ForEach(ForEachSerializerFn fn, void* ctx) const
    {
        if (!fn)
        {
            return;
        }

        for (uint32_t i = 0; i < m_ids.size(); ++i)
        {
            fn(ctx, m_ids[i], m_fns[i]);
        }
    }

    uint32_t ComponentSerializerRegistry::Count() const
    {
        return static_cast<uint32_t>(m_ids.size());
    }
} // namespace ZEngine::ECS

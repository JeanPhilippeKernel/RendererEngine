#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    ComponentReflectionRegistry& ComponentReflectionRegistry::Get()
    {
        static ComponentReflectionRegistry s_instance;
        return s_instance;
    }

    void ComponentReflectionRegistry::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "ComponentReflectionRegistry::Initialize: arena must not be null")
        m_arena = arena;
        m_meta.init(arena, 64);
    }

    void ComponentReflectionRegistry::Register(const ComponentMeta& meta)
    {
        ZENGINE_VALIDATE_ASSERT(m_arena != nullptr, "ComponentReflectionRegistry::Register: Initialize() must be called first")
        ZENGINE_VALIDATE_ASSERT(meta.TypeName != nullptr, "ComponentReflectionRegistry::Register: meta.TypeName must not be null")
        ZENGINE_VALIDATE_ASSERT(meta.FieldCount == 0 || meta.Fields != nullptr, "ComponentReflectionRegistry::Register: meta.Fields is null but FieldCount > 0")

        if (const ComponentMeta* existing = Lookup(meta.TypeID))
        {
            ZENGINE_CORE_WARN("ComponentReflectionRegistry: TypeID {} already registered as '{}', ignoring '{}'", meta.TypeID, existing->TypeName, meta.TypeName);
            return;
        }

        m_meta.push(meta);
    }

    const ComponentMeta* ComponentReflectionRegistry::Lookup(ComponentTypeID id) const
    {
        for (uint32_t i = 0; i < m_meta.size(); ++i)
        {
            if (m_meta[i].TypeID == id)
            {
                return &m_meta[i];
            }
        }
        return nullptr;
    }

    const ComponentMeta* ComponentReflectionRegistry::LookupByName(cstring type_name) const
    {
        if (!type_name)
        {
            return nullptr;
        }

        for (uint32_t i = 0; i < m_meta.size(); ++i)
        {
            if (m_meta[i].TypeName && Helpers::secure_strcmp(m_meta[i].TypeName, type_name) == 0)
            {
                return &m_meta[i];
            }
        }
        return nullptr;
    }

    uint32_t ComponentReflectionRegistry::Count() const
    {
        return static_cast<uint32_t>(m_meta.size());
    }
} // namespace ZEngine::ECS

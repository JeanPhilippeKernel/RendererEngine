#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ECS/Reflection/ComponentMeta.h>

namespace ZEngine::ECS
{

    class ComponentReflectionRegistry
    {
    public:
        static ComponentReflectionRegistry& Get();

        void                                Initialize(Core::Memory::ArenaAllocator* arena);

        void                                Register(const ComponentMeta& meta);

        [[nodiscard]] const ComponentMeta*  Lookup(ComponentTypeID id) const;
        [[nodiscard]] const ComponentMeta*  LookupByName(const char* type_name) const;

        template <typename Fn>
        void ForEach(Fn&& fn) const
        {
            for (uint32_t i = 0; i < m_meta.size(); ++i)
            {
                fn(m_meta[i]);
            }
        }

        [[nodiscard]] uint32_t Count() const;

    private:
        Core::Containers::Array<ComponentMeta> m_meta;
        Core::Memory::ArenaAllocator*          m_arena = nullptr;
    };

} // namespace ZEngine::ECS

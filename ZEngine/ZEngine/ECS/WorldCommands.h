#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstdint>

namespace ZEngine::ECS
{
    class Scene;

    // Callback invoked in Flush() after a SpawnEntity command is applied.
    // Plain fn-ptr + context to avoid std::function heap allocation.
    struct SpawnCallback
    {
        void* Context               = nullptr;
        void (*Fn)(void*, EntityID) = nullptr;

        bool IsValid() const
        {
            return Fn != nullptr;
        }
        void Invoke(EntityID id) const
        {
            if (Fn)
                Fn(Context, id);
        }
    };

    class WorldCommands
    {
    public:
        static constexpr uint32_t MAX_COMPONENT_DATA_BYTES = 256;

        void                      Initialize(Core::Memory::ArenaAllocator* arena);

        void                      SpawnEntity(SpawnCallback cb = {});
        void                      DestroyEntity(EntityID id);

        template <typename T>
        void AddComponent(EntityID id, T component)
        {
            static_assert(sizeof(T) <= MAX_COMPONENT_DATA_BYTES, "WorldCommands::AddComponent: component exceeds MAX_COMPONENT_DATA_BYTES");

            Command cmd{};
            cmd.Kind   = CommandKind::AddComponent;
            cmd.Target = id;
            cmd.TypeID = ComponentTypeOf<T>();
            cmd.Apply  = [](Scene& scene, EntityID target, const uint8_t* data) {
                T comp{};
                Helpers::secure_memcpy(&comp, sizeof(T), data, sizeof(T));
                scene.AddComponent<T>(target, static_cast<T&&>(comp));
            };
            Helpers::secure_memcpy(cmd.Data, MAX_COMPONENT_DATA_BYTES, &component, sizeof(T));
            m_commands.push(cmd);
        }

        template <typename T>
        void RemoveComponent(EntityID id)
        {
            Command cmd{};
            cmd.Kind   = CommandKind::RemoveComponent;
            cmd.Target = id;
            cmd.TypeID = ComponentTypeOf<T>();
            cmd.Apply  = [](Scene& scene, EntityID target, const uint8_t*) { scene.RemoveComponent<T>(target); };
            m_commands.push(cmd);
        }

        void Flush(Scene& scene);
        void Clear();

        bool IsEmpty() const
        {
            return m_commands.empty() && m_spawn_callbacks.empty();
        }

    private:
        enum class CommandKind : uint8_t
        {
            SpawnEntity,
            DestroyEntity,
            AddComponent,
            RemoveComponent,
        };

        using ApplyFn = void (*)(Scene&, EntityID, const uint8_t*);

        struct Command
        {
            CommandKind     Kind                           = CommandKind::SpawnEntity;
            EntityID        Target                         = INVALID_ENTITY;
            ComponentTypeID TypeID                         = 0;
            ApplyFn         Apply                          = nullptr;
            uint8_t         Data[MAX_COMPONENT_DATA_BYTES] = {};
            int32_t         SpawnCallbackIndex             = -1; // index into m_spawn_callbacks; -1 = none
        };

        Core::Containers::Array<Command>       m_commands;
        Core::Containers::Array<SpawnCallback> m_spawn_callbacks;
    };
} // namespace ZEngine::ECS

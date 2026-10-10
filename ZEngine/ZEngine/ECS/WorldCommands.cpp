#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    void WorldCommands::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_commands.init(arena, 256);
        m_spawn_callbacks.init(arena, 64);
    }

    void WorldCommands::SpawnEntity(SpawnCallback cb)
    {
        Command cmd{};
        cmd.Kind = CommandKind::SpawnEntity;

        if (cb.IsValid())
        {
            cmd.SpawnCallbackIndex = static_cast<int32_t>(m_spawn_callbacks.size());
            m_spawn_callbacks.push(cb);
        }

        m_commands.push(cmd);
    }

    void WorldCommands::DestroyEntity(EntityID id)
    {
        // Deduplicate: skip if already queued for destruction
        for (size_t i = 0; i < m_commands.size(); ++i)
        {
            if (m_commands[i].Kind == CommandKind::DestroyEntity && m_commands[i].Target == id)
                return;
        }

        Command cmd{};
        cmd.Kind   = CommandKind::DestroyEntity;
        cmd.Target = id;
        m_commands.push(cmd);
    }

    void WorldCommands::Flush(Scene& scene)
    {
        for (size_t i = 0; i < m_commands.size(); ++i)
        {
            Command& cmd = m_commands[i];

            switch (cmd.Kind)
            {
                case CommandKind::SpawnEntity:
                {
                    EntityID id = scene.CreateEntity();
                    if (cmd.SpawnCallbackIndex >= 0)
                    {
                        SpawnCallback& cb = m_spawn_callbacks[static_cast<size_t>(cmd.SpawnCallbackIndex)];
                        cb.Invoke(id);
                    }
                    break;
                }

                case CommandKind::DestroyEntity:
                {
                    // Guard: entity may have been destroyed by a prior command in this flush
                    if (scene.IsAlive(cmd.Target))
                        scene.DestroyEntity(cmd.Target);
                    break;
                }

                case CommandKind::AddComponent:
                case CommandKind::RemoveComponent:
                {
                    ZENGINE_VALIDATE_ASSERT(cmd.Apply != nullptr, "WorldCommands::Flush: component command has null Apply fn")
                    if (scene.IsAlive(cmd.Target))
                        cmd.Apply(scene, cmd.Target, cmd.Data);
                    break;
                }
            }
        }

        Clear();
    }

    void WorldCommands::Merge(const WorldCommands& src)
    {
        if (src.IsEmpty())
            return;

        // Callbacks from src are appended after our existing ones, so indices
        // in src's SpawnEntity commands must be shifted by the current count.
        int32_t callback_offset = static_cast<int32_t>(m_spawn_callbacks.size());

        for (size_t i = 0; i < src.m_spawn_callbacks.size(); ++i)
            m_spawn_callbacks.push(src.m_spawn_callbacks[i]);

        for (size_t i = 0; i < src.m_commands.size(); ++i)
        {
            Command cmd = src.m_commands[i];
            if (cmd.Kind == CommandKind::SpawnEntity && cmd.SpawnCallbackIndex >= 0)
                cmd.SpawnCallbackIndex += callback_offset;
            m_commands.push(cmd);
        }
    }

    void WorldCommands::Clear()
    {
        // Reset size to 0; arena memory is not reclaimed (reused next frame)
        while (!m_commands.empty())
            m_commands.pop();
        while (!m_spawn_callbacks.empty())
            m_spawn_callbacks.pop();
    }
} // namespace ZEngine::ECS

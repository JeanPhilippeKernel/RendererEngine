#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    void ActorManager::Initialize(Core::Memory::ArenaAllocator* arena, Scene& scene)
    {
        m_arena = arena;
        m_scene = &scene;
        m_handles.Initialize(arena, MAX_ACTORS);
    }

    void ActorManager::Destroy(ActorHandle handle)
    {
        if (!m_handles.IsLive(handle))
            return;

        Actor** ptr = m_handles.Access(handle);
        if (ptr && *ptr)
        {
            Actor* actor = *ptr;
            actor->OnDestroy();
            if (m_scene->IsAlive(actor->m_entity_id))
                m_scene->DestroyEntity(actor->m_entity_id);
            // Actor memory is arena-owned — no delete; destructor is called explicitly
            actor->~Actor();
        }

        m_handles.Remove(handle);
    }

    Actor* ActorManager::Access(ActorHandle handle)
    {
        if (!m_handles.IsLive(handle))
            return nullptr;
        Actor** ptr = m_handles.Access(handle);
        return ptr ? *ptr : nullptr;
    }

    const Actor* ActorManager::Access(ActorHandle handle) const
    {
        if (!m_handles.IsLive(handle))
            return nullptr;
        Actor** ptr = const_cast<Helpers::HandleManager<Actor*>&>(m_handles).Access(handle);
        return ptr ? *ptr : nullptr;
    }

    bool ActorManager::IsLive(ActorHandle handle) const
    {
        return m_handles.IsLive(handle);
    }

    void ActorManager::Tick(float dt)
    {
        uint32_t head = m_handles.Head();
        for (uint32_t i = 0; i < head; ++i)
        {
            ActorHandle h = m_handles.ToHandle(i);
            if (!m_handles.IsLive(h))
                continue;
            Actor** ptr = m_handles.Access(h);
            if (ptr && *ptr)
                (*ptr)->OnTick(dt);
        }
    }

    void ActorManager::Shutdown()
    {
        // Destroy all live Actors in reverse creation order (head-1 down to 0)
        uint32_t head = m_handles.Head();
        for (int64_t i = static_cast<int64_t>(head) - 1; i >= 0; --i)
        {
            ActorHandle h = m_handles.ToHandle(static_cast<uint32_t>(i));
            if (m_handles.IsLive(h))
                Destroy(h);
        }
        m_scene = nullptr;
        m_arena = nullptr;
    }
} // namespace ZEngine::ECS

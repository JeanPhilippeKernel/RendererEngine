#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/Actor.h>
#include <ZEngine/Helpers/HandleManager.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    class Scene;

    // Handle to an Actor — generational index, 16 bytes, no ref count.
    using ActorHandle = Helpers::Handle<Actor*>;

    class ActorManager
    {
    public:
        static constexpr uint32_t MAX_ACTORS = 1024;

        void                      Initialize(Core::Memory::ArenaAllocator* arena, Scene& scene);

        // Placement-allocates a T (must derive from Actor) from the arena,
        // creates an EntityID, calls OnCreate(), returns a generational handle.
        template <typename T = Actor>
        ActorHandle Create()
        {
            static_assert(sizeof(T) <= 4096, "Actor subclass exceeds maximum size");

            ZENGINE_VALIDATE_ASSERT(m_arena != nullptr, "ActorManager::Create: not initialized")

            // Allocate T from arena
            T* actor           = ZPushStructCtor(m_arena, T);

            // Wire entity
            actor->m_entity_id = m_scene->CreateEntity();
            actor->m_scene     = m_scene;

            // Store pointer in handle array
            Actor**     slot   = nullptr;
            ActorHandle handle = m_handles.Add(static_cast<Actor*>(actor));
            ZENGINE_VALIDATE_ASSERT(handle.Valid(), "ActorManager::Create: MAX_ACTORS capacity reached")

            actor->OnCreate();
            return handle;
        }

        // Calls OnDestroy(), destroys the EntityID, frees the handle slot.
        // Stale handles are silently ignored.
        void         Destroy(ActorHandle handle);

        Actor*       Access(ActorHandle handle);
        const Actor* Access(ActorHandle handle) const;

        bool         IsLive(ActorHandle handle) const;

        // Calls OnTick(dt) on all live Actors.
        void         Tick(float dt);

        // Destroys all live Actors, then releases the handle array.
        // Must be called before Scene::Shutdown().
        void         Shutdown();

        uint32_t     Count() const
        {
            return static_cast<uint32_t>(m_handles.Size());
        }

    private:
        Helpers::HandleManager<Actor*> m_handles;
        Core::Memory::ArenaAllocator*  m_arena = nullptr;
        Scene*                         m_scene = nullptr;
    };
} // namespace ZEngine::ECS

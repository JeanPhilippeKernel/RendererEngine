#pragma once
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS
{
    // Reusable query — pre-computes ArchetypeMask once at construction.
    // Fn must be callable as: void(EntityID, Ts&...)
    template <typename... Ts>
    class Query
    {
    public:
        explicit Query(Scene& scene) : m_scene(scene)
        {
            m_mask = (MaskBit(ComponentTypeOf<Ts>()) | ...);
        }

        template <typename Fn>
        void ForEach(Fn&& fn)
        {
            m_scene.ForEach<Ts...>(static_cast<Fn&&>(fn));
        }

    private:
        Scene&        m_scene;
        ArchetypeMask m_mask = 0;
    };
} // namespace ZEngine::ECS

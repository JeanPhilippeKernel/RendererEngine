#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>

namespace ZEngine::ECS
{
    // SystemFn: void(Scene&, float dt, WorldCommands&)
    // Raw fn-ptr — no std::function heap allocation.
    // WorldCommands& lets systems enqueue deferred mutations safely during parallel execution.
    using SystemFn = void (*)(Scene&, float, WorldCommands&);
    using SystemID = uint32_t;

    struct SystemDeps
    {
        ArchetypeMask ReadMask  = 0;
        ArchetypeMask WriteMask = 0;
    };

    class WorldTick
    {
    public:
        static constexpr uint32_t MAX_SYSTEMS = 64;

        void                      Initialize(Core::Memory::ArenaAllocator* arena);

        // Register a system. Returns a stable SystemID for use with OrderBefore.
        // Must be called before Commit(). Return value must not be discarded.
        [[nodiscard]] SystemID    RegisterSystem(SystemFn fn, SystemDeps deps);

        // Declare that system a must complete before system b starts.
        // Required whenever a and b conflict (see system-scheduler.md section 4).
        void                      OrderBefore(SystemID a, SystemID b);

        // Build the DAG. Call once after all RegisterSystem/OrderBefore calls and
        // before the first Tick. Asserts on unresolved conflicts and cycles.
        void                      Commit();

        // Execute all systems for one frame.
        // Single-system waves run inline on the calling thread.
        // Multi-system waves dispatch to ThreadPoolHelper with a spin-yield barrier.
        // Blocks until all waves complete. Caller must call commands.Flush(scene) after.
        void                      Tick(Scene& scene, float dt, WorldCommands& commands);

        uint32_t                  SystemCount() const
        {
            return static_cast<uint32_t>(m_nodes.size());
        }
        uint32_t WaveCount() const
        {
            return static_cast<uint32_t>(m_waves.size());
        }

    private:
        struct SystemNode
        {
            SystemFn                          Fn       = nullptr;
            SystemDeps                        Deps     = {};
            uint32_t                          Index    = 0;
            uint32_t                          InDegree = 0;
            Core::Containers::Array<uint32_t> Successors;
        };

        Core::Containers::Array<SystemNode>                        m_nodes;
        Core::Containers::Array<Core::Containers::Array<uint32_t>> m_waves;
        Core::Containers::Array<uint32_t>                          m_order_edges_from;
        Core::Containers::Array<uint32_t>                          m_order_edges_to;
        Core::Memory::ArenaAllocator*                              m_arena     = nullptr;
        bool                                                       m_committed = false;

        bool                                                       HasConflict(const SystemNode& a, const SystemNode& b) const;
        bool                                                       HasOrderEdge(uint32_t a, uint32_t b) const;
        void                                                       BuildEdges();
        void                                                       TopologicalSort();
    };
} // namespace ZEngine::ECS

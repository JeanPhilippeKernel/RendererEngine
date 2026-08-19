#include <ZEngine/ECS/WorldTick.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/ZEngineDef.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ZEngine::ECS
{
    void WorldTick::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_arena = arena;
        m_nodes.init(arena, MAX_SYSTEMS);
        m_waves.init(arena, MAX_SYSTEMS);
        m_order_edges_from.init(arena, MAX_SYSTEMS * 4);
        m_order_edges_to.init(arena, MAX_SYSTEMS * 4);
    }

    SystemID WorldTick::RegisterSystem(SystemFn fn, SystemDeps deps)
    {
        ZENGINE_VALIDATE_ASSERT(!m_committed, "WorldTick::RegisterSystem called after Commit()")
        ZENGINE_VALIDATE_ASSERT(fn != nullptr, "WorldTick::RegisterSystem: fn must not be null")
        ZENGINE_VALIDATE_ASSERT(m_nodes.size() < MAX_SYSTEMS, "WorldTick::RegisterSystem: MAX_SYSTEMS reached")

        SystemID   id = static_cast<SystemID>(m_nodes.size());

        SystemNode node{};
        node.Fn       = fn;
        node.Deps     = deps;
        node.Index    = id;
        node.InDegree = 0;
        node.Successors.init(m_arena, 8);
        m_nodes.push(std::move(node));

        return id;
    }

    void WorldTick::OrderBefore(SystemID a, SystemID b)
    {
        ZENGINE_VALIDATE_ASSERT(!m_committed, "WorldTick::OrderBefore called after Commit()")
        ZENGINE_VALIDATE_ASSERT(a < m_nodes.size(), "WorldTick::OrderBefore: invalid SystemID a")
        ZENGINE_VALIDATE_ASSERT(b < m_nodes.size(), "WorldTick::OrderBefore: invalid SystemID b")
        ZENGINE_VALIDATE_ASSERT(a != b, "WorldTick::OrderBefore: a and b must be different systems")

        m_order_edges_from.push(a);
        m_order_edges_to.push(b);
    }

    bool WorldTick::HasConflict(const SystemNode& a, const SystemNode& b) const
    {
        return (a.Deps.WriteMask & b.Deps.ReadMask) != 0 || (a.Deps.WriteMask & b.Deps.WriteMask) != 0 || (a.Deps.ReadMask & b.Deps.WriteMask) != 0;
    }

    bool WorldTick::HasOrderEdge(uint32_t a, uint32_t b) const
    {
        for (size_t i = 0; i < m_order_edges_from.size(); ++i)
        {
            if ((m_order_edges_from[i] == a && m_order_edges_to[i] == b) || (m_order_edges_from[i] == b && m_order_edges_to[i] == a))
                return true;
        }
        return false;
    }

    void WorldTick::BuildEdges()
    {
        uint32_t n = static_cast<uint32_t>(m_nodes.size());

        for (uint32_t i = 0; i < n; ++i)
        {
            for (uint32_t j = i + 1; j < n; ++j)
            {
                if (!HasConflict(m_nodes[i], m_nodes[j]))
                    continue;

                ZENGINE_VALIDATE_ASSERT(HasOrderEdge(i, j), "WorldTick::Commit: two systems conflict on a component but no OrderBefore edge exists between them")

                // Determine direction from the declared edge
                bool a_before_b = false;
                for (size_t e = 0; e < m_order_edges_from.size(); ++e)
                {
                    if (m_order_edges_from[e] == i && m_order_edges_to[e] == j)
                    {
                        a_before_b = true;
                        break;
                    }
                }

                uint32_t from = a_before_b ? i : j;
                uint32_t to   = a_before_b ? j : i;

                m_nodes[from].Successors.push(to);
                m_nodes[to].InDegree++;
            }
        }

        // Also insert non-conflict ordering edges (logical sequencing)
        for (size_t e = 0; e < m_order_edges_from.size(); ++e)
        {
            uint32_t from          = m_order_edges_from[e];
            uint32_t to            = m_order_edges_to[e];

            // Check if this edge is already in Successors (added via conflict path above)
            bool     already_added = false;
            for (size_t s = 0; s < m_nodes[from].Successors.size(); ++s)
            {
                if (m_nodes[from].Successors[s] == to)
                {
                    already_added = true;
                    break;
                }
            }
            if (!already_added)
            {
                m_nodes[from].Successors.push(to);
                m_nodes[to].InDegree++;
            }
        }
    }

    void WorldTick::TopologicalSort()
    {
        uint32_t                          n = static_cast<uint32_t>(m_nodes.size());

        // Kahn's algorithm: process nodes with InDegree == 0 in waves
        Core::Containers::Array<uint32_t> current_wave;
        current_wave.init(m_arena, n);

        // Snapshot InDegrees — Kahn's modifies them
        Core::Containers::Array<uint32_t> in_degree;
        in_degree.init(m_arena, n);
        for (uint32_t i = 0; i < n; ++i)
            in_degree.push(m_nodes[i].InDegree);

        uint32_t processed = 0;

        while (true)
        {
            // Collect all nodes with in_degree == 0 that haven't been placed yet
            for (size_t i = 0; i < in_degree.size(); ++i)
            {
                if (in_degree[i] == 0)
                {
                    current_wave.push(static_cast<uint32_t>(i));
                    in_degree[i] = UINT32_MAX; // mark as placed
                }
            }

            if (current_wave.empty())
                break;

            // Record this wave (copy into a new Array)
            Core::Containers::Array<uint32_t> wave;
            wave.init(m_arena, static_cast<uint32_t>(current_wave.size()));
            for (size_t i = 0; i < current_wave.size(); ++i)
                wave.push(current_wave[i]);
            m_waves.push(std::move(wave));

            processed += static_cast<uint32_t>(current_wave.size());

            // Reduce in_degree for successors
            for (size_t i = 0; i < current_wave.size(); ++i)
            {
                uint32_t node_idx = current_wave[i];
                for (size_t s = 0; s < m_nodes[node_idx].Successors.size(); ++s)
                {
                    uint32_t succ = m_nodes[node_idx].Successors[s];
                    if (in_degree[succ] != UINT32_MAX)
                        in_degree[succ]--;
                }
            }

            current_wave.clear();
        }

        ZENGINE_VALIDATE_ASSERT(processed == n, "WorldTick::Commit: cycle detected in system dependency graph — check OrderBefore declarations")
    }

    void WorldTick::Commit()
    {
        ZENGINE_VALIDATE_ASSERT(!m_committed, "WorldTick::Commit called more than once")
        ZENGINE_VALIDATE_ASSERT(m_nodes.size() > 0, "WorldTick::Commit: no systems registered")

        BuildEdges();
        TopologicalSort();

        // Pre-allocate one staging WorldCommands per system. These are reused
        // every frame: Clear() resets size to 0 without releasing arena memory,
        // so pushes stay allocation-free after the first exercised frame.
        uint32_t n = static_cast<uint32_t>(m_nodes.size());
        m_staging.init(m_arena, n, n);
        for (uint32_t i = 0; i < n; ++i)
            m_staging[i].Initialize(m_arena);

        m_committed = true;
    }

    void WorldTick::Tick(Scene& scene, float dt, WorldCommands& commands)
    {
        ZENGINE_VALIDATE_ASSERT(m_committed, "WorldTick::Tick called before Commit()")

        for (size_t w = 0; w < m_waves.size(); ++w)
        {
            const Core::Containers::Array<uint32_t>& wave = m_waves[w];

            // Single-system wave: run inline on the calling thread.
            // Uses the caller's commands buffer directly — zero overhead.
            if (wave.size() == 1)
            {
                m_nodes[wave[0]].Fn(scene, dt, commands);
                continue;
            }

            // Multi-system wave: each system writes to its own staging buffer
            // so concurrent calls to WorldCommands never touch shared state.
            // After the wave barrier the main thread merges staging buffers into
            // the authoritative commands buffer in wave-submission order.
            for (size_t i = 0; i < wave.size(); ++i)
                m_staging[wave[i]].Clear();

            std::atomic<uint32_t>   remaining{static_cast<uint32_t>(wave.size())};
            std::mutex              mtx;
            std::condition_variable cv;

            for (size_t i = 0; i < wave.size(); ++i)
            {
                uint32_t       node_idx = wave[i];
                SystemFn       fn       = m_nodes[node_idx].Fn;
                WorldCommands* staging  = &m_staging[node_idx];

                ZEngine::Helpers::ThreadPoolHelper::Submit([&scene, dt, fn, staging, &remaining, &cv]() {
                    struct Guard
                    {
                        std::atomic<uint32_t>&   r;
                        std::condition_variable& cv;
                        ~Guard()
                        {
                            if (r.fetch_sub(1, std::memory_order_acq_rel) == 1)
                                cv.notify_one();
                        }
                    } guard{remaining, cv};
                    fn(scene, dt, *staging);
                });
            }

            // Phase 1: spin-yield — stays in user space for fast waves.
            for (int spin = 0; spin < 100; ++spin)
            {
                if (remaining.load(std::memory_order_acquire) == 0)
                    break;
                std::this_thread::yield();
            }

            // Phase 2: cv.wait fallback for slow waves.
            if (remaining.load(std::memory_order_acquire) != 0)
            {
                std::unique_lock<std::mutex> lock(mtx);
                static constexpr int         kWaveTimeoutSeconds = 30;
                bool                         completed           = cv.wait_for(lock, std::chrono::seconds(kWaveTimeoutSeconds), [&remaining] { return remaining.load(std::memory_order_relaxed) == 0; });
                ZENGINE_VALIDATE_ASSERT(completed, "WorldTick::Tick: wave timed out — a system has hung or crashed")
            }

            // Merge staging buffers into the authoritative buffer in submission order.
            for (size_t i = 0; i < wave.size(); ++i)
                commands.Merge(m_staging[wave[i]]);
        }
    }
} // namespace ZEngine::ECS

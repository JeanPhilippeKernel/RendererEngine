#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    // One flattened Read/Write event, used to derive RAW/WAW/WAR hazard edges.
    struct RGEvent
    {
        uint32_t ResourceIndex = UINT32_MAX;
        uint32_t PassIndex     = UINT32_MAX;
        bool     IsWrite       = false;
    };

    // One RAW/WAW/WAR hazard edge between two passes.
    struct RGEdge
    {
        uint32_t From = UINT32_MAX;
        uint32_t To   = UINT32_MAX;
    };

    /// @brief Computes a real execution order for `passes` from their Reads/Writes,
    ///        via Kahn's algorithm with a lowest-index tie-break. Device-independent —
    ///        reusable from unit tests.
    /// @details Edges come from the standard hazard triad: RAW (read-after-write — a
    ///          reader must run after the last writer), WAW (write-after-write — two
    ///          writers of the same resource keep declaration order), and WAR
    ///          (write-after-read — a new writer must run after every reader of the
    ///          prior version). Same-pass self-edges are dropped.
    /// @param scratch_arena Used only for this call's temporary bookkeeping.
    /// @param out_order Must already be initialized by the caller; cleared and
    ///        repopulated on success.
    /// @param out_cycle_pass_index On a cycle, receives one pass index in the cycle.
    /// @return false on a dependency cycle (caller should fall back to declaration order).
    bool BuildPassTopology(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::Array<uint32_t>& out_order, uint32_t* out_cycle_pass_index);

} // namespace ZEngine::Rendering::Renderers

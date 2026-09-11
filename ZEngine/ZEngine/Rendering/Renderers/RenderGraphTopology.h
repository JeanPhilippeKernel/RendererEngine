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
        uint32_t Version       = 0;
        uint32_t PassIndex     = UINT32_MAX;
        bool     IsWrite       = false;
    };

    // One RAW/WAW/WAR hazard edge between two passes.
    struct RGEdge
    {
        uint32_t From = UINT32_MAX;
        uint32_t To   = UINT32_MAX;
    };

    /// @brief Groups an already-topologically-sorted graph into contiguous queue
    /// batches. The function is device-independent so capability fallback and
    /// scheduling rules are unit-testable without Vulkan.
    void BuildQueueBatches(Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<uint32_t> order, bool has_separate_transfer_queue, bool has_separate_compute_queue, Core::Containers::Array<RGQueueBatch>& out_batches);

    /// @brief Derives the minimal cross-queue timeline waits needed by a queue
    /// batch plan. Repeated hazards between the same batches are deduplicated.
    void BuildQueueDependencies(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<uint32_t> order, Core::Containers::ArrayView<RGQueueBatch> batches, uint32_t resource_count, Core::Containers::Array<RGQueueDependency>& out_dependencies);

    /// @brief Produces one release/acquire ownership hand-off per resource use
    /// crossing distinct resolved queue batches.
    void BuildQueueOwnershipTransfers(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<uint32_t> order, Core::Containers::ArrayView<RGQueueBatch> batches, uint32_t resource_count, Core::Containers::Array<RGQueueOwnershipTransfer>& out_transfers);

    enum class RGDeclarationError : uint8_t
    {
        None,
        InvalidHandle,
        DuplicateProducer,
        MissingProducer,
        MissingImport,
    };

    struct RGDeclarationValidationResult
    {
        RGDeclarationError Error         = RGDeclarationError::None;
        uint32_t           ResourceIndex = UINT32_MAX;
        uint32_t           Version       = 0;
        uint32_t           PassIndex     = UINT32_MAX;
        uint32_t           WriterCount   = 0;
    };

    /// @brief Validates enabled pass declarations without allocating GPU resources.
    /// Version zero is valid only for an imported resource; every later version
    /// read requires exactly one enabled producer.
    bool ValidatePassDeclarations(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<RGResource> resources, RGDeclarationValidationResult* out_result = nullptr);

    /// @brief Computes a real execution order for `passes` from their Reads/Writes,
    ///        via Kahn's algorithm with a lowest-index tie-break. Device-independent —
    ///        reusable from unit tests.
    /// @details Disabled passes are excluded; toggling one must be followed by a graph
    ///          recompile. Edges come from the standard hazard triad: RAW (read-after-write — a
    ///          reader must run after the last writer), WAW (write-after-write — two
    ///          writers of the same resource keep declaration order), and WAR
    ///          (write-after-read — a new writer must run after every reader of the
    ///          prior version). Same-pass self-edges are dropped.
    /// @param scratch_arena Used only for this call's temporary bookkeeping.
    /// @param out_order Must already be initialized by the caller; cleared and
    ///        repopulated on success.
    /// @param out_cycle_pass_index On a cycle, receives one pass index in the cycle.
    /// @return false on a dependency cycle. Compilation must reject that graph.
    bool BuildPassTopology(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::Array<uint32_t>& out_order, uint32_t* out_cycle_pass_index);

} // namespace ZEngine::Rendering::Renderers

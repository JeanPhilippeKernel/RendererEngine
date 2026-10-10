#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief One flattened resource access used to derive RAW, WAW, and WAR edges.
    struct RGEvent
    {
        uint32_t ResourceIndex = UINT32_MAX;
        uint32_t Version       = 0;
        uint32_t PassIndex     = UINT32_MAX;
        bool     IsWrite       = false;
    };

    /// @brief Groups an already-topologically-sorted graph into contiguous queue
    /// batches. The function is device-independent so capability fallback and
    /// scheduling rules are unit-testable without Vulkan.
    void BuildQueueBatches(Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<uint32_t> order, bool has_separate_transfer_queue, bool has_separate_compute_queue, Core::Containers::Array<RGQueueBatch>& out_batches);

    /// @brief Derives cross-queue timeline waits from canonical pass dependencies.
    /// Repeated dependencies between the same batches are deduplicated.
    void BuildQueueDependencies(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<uint32_t> order, Core::Containers::ArrayView<RGQueueBatch> batches, Core::Containers::ArrayView<RGPassDependency> pass_dependencies, Core::Containers::Array<RGQueueDependency>& out_dependencies);

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
    /// @details Disabled and culled passes are excluded. A reader depends on the
    /// exact version it declares; consecutive versions retain an in-place hazard
    /// edge until the transient allocator binds them to separate physical storage.
    /// @param scratch_arena Used only for this call's temporary bookkeeping.
    /// @param out_order Must already be initialized by the caller; cleared and
    ///        repopulated on success.
    /// @param out_cycle_pass_index On a cycle, receives one pass index in the cycle.
    /// @return false on a dependency cycle. Compilation must reject that graph.
    bool BuildPassTopology(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::Array<uint32_t>& out_order, uint32_t* out_cycle_pass_index, Core::Containers::Array<RGPassDependency>* out_dependencies = nullptr);

    /// @brief Groups a topological order into deterministic dependency levels.
    void BuildTopologyLevels(Core::Memory::ArenaAllocator* scratch_arena, Core::Memory::ArenaAllocator* output_arena, Core::Containers::ArrayView<uint32_t> order, Core::Containers::ArrayView<RGPassDependency> dependencies, uint32_t pass_count, Core::Containers::Array<RGTopologyLevel>& out_levels);

    /// @brief Marks passes unreachable from an explicit graph sink as culled.
    /// @details Swapchain writers, exported versions, and NeverCull passes are
    /// sinks. Dependencies must describe the full enabled graph before calling.
    void CullPasses(Core::Memory::ArenaAllocator* scratch_arena, Core::Containers::ArrayView<RGPass> passes, Core::Containers::ArrayView<RGResource> resources, Core::Containers::ArrayView<RGPassDependency> dependencies, Core::Containers::ArrayView<RGExportedResource> exports);

} // namespace ZEngine::Rendering::Renderers

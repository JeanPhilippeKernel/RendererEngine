#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Materials/MaterialInstance.h>

namespace ZEngine::Rendering::Materials
{
    /// @brief One draw item with the state and ordering inputs used by the material system.
    struct MaterialDrawItem
    {
        const MaterialInstance* Material        = nullptr;
        uint64_t                PSOKeyHash      = 0;
        uint32_t                MaterialIndex   = 0;
        float                   ViewDepth       = 0.0f;
        uint32_t                SubmissionOrder = 0;
    };

    /// @brief Separate render-ready opaque and conventional-transparent draw buckets.
    struct MaterialDrawLists
    {
        Core::Containers::Array<MaterialDrawItem> Opaque      = {};
        Core::Containers::Array<MaterialDrawItem> Transparent = {};
    };

    /// @brief Produces deterministic opaque state sorting and conventional transparency ordering.
    class DrawSorter
    {
    public:
        /// @brief Partitions and stably sorts draw items using memory owned by `arena`.
        /// @details Opaque draws sort by PSO key, material index, then front-to-back depth.
        /// Transparent draws sort strictly back-to-front and use submission order only as
        /// their tie-breaker; they never undergo PSO or material state reordering.
        static void Sort(Core::Memory::ArenaAllocator* arena, Core::Containers::ArrayView<const MaterialDrawItem> draws, MaterialDrawLists* out_lists);
    };
} // namespace ZEngine::Rendering::Materials

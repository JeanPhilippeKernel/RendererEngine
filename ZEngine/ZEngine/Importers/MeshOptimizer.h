#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <meshoptimizer.h>
#include <cstdlib>

namespace ZEngine::Importers
{
    // Three-pass mesh optimization for one submesh.
    // Operates on RELATIVE (0-based) indices and a pointer to the submesh's vertex slice.
    // Vertex format: 8 floats (pos.xyz  nrm.xyz  uv.xy) — 32 bytes per vertex.
    //
    // Pass 1: vertex cache    — reorders indices for GPU post-transform cache
    // Pass 2: overdraw        — clusters triangles to reduce pixel shader overdraw
    // Pass 3: vertex fetch    — reorders vertices to match optimized index order
    //
    // The temp buffer for Pass 3 uses the system heap (malloc/free) so it is
    // freed immediately per submesh — arena accumulation crashes are avoided.
    // This is acceptable since optimization is an import-time, one-shot operation.
    inline void OptimizeMeshSubmesh(float* vertices, uint32_t vertex_count, uint32_t* indices, uint32_t index_count)
    {
        if (!vertices || !indices || vertex_count == 0 || index_count == 0)
            return;

        constexpr size_t stride = 8 * sizeof(float); // 32 bytes per vertex

        // Pass 1 — vertex cache
        meshopt_optimizeVertexCache(indices, indices, index_count, vertex_count);

        // Pass 2 — overdraw (position at offset 0, stride 32)
        meshopt_optimizeOverdraw(indices, indices, index_count, vertices, vertex_count, stride, 1.05f);

        // Pass 3 — vertex fetch: system heap for the temp buffer, freed immediately.
        float* opt = static_cast<float*>(std::malloc(vertex_count * stride));
        if (!opt)
            return;

        meshopt_optimizeVertexFetch(opt, indices, index_count, vertices, vertex_count, stride);
        Helpers::secure_memcpy(vertices, vertex_count * stride, opt, vertex_count * stride);
        std::free(opt);
    }
} // namespace ZEngine::Importers

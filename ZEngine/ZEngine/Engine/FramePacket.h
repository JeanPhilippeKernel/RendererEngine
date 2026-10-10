#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/RenderableTransform.h>
#include <cstdint>

namespace ZEngine::Timing
{
    // Per-frame snapshot written by MainThreadRun and consumed by RenderThreadRun.
    // Carries the interpolation alpha and the interpolated ECS transform array.
    //
    // Double-buffering: Engine maintains two FramePackets (slots 0 and 1).
    // MainThreadRun writes to slot (write_index % 2).
    // RenderThreadRun reads from slot ((write_index + 1) % 2).
    // After writing: write_index.fetch_add(1, release) — no mutex needed (SPSC).
    //
    // Pre-allocate the Transforms array at scene load time to avoid per-frame growth.
    struct FramePacket
    {
        float                                             Alpha{0.f}; // interpolation factor in [0, 1)
        float                                             RawDeltaSeconds{0.f};
        uint64_t                                          FrameIndex{0};

        // Interpolated ECS transforms — filled by Scene::FillRenderableTransforms(alpha, out).
        // Reuses ECS::RenderableTransform to avoid duplicating the struct.
        Core::Containers::Array<ECS::RenderableTransform> Transforms;

        void                                              Initialize(Core::Memory::ArenaAllocator* arena, uint32_t max_entities)
        {
            Transforms.init(arena, max_entities);
        }

        void Clear()
        {
            Alpha           = 0.f;
            RawDeltaSeconds = 0.f;
            while (!Transforms.empty())
                Transforms.pop();
        }
    };
} // namespace ZEngine::Timing

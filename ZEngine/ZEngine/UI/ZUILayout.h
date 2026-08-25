#pragma once
#include <ZEngine/UI/ZUIContext.h>

namespace ZEngine::UI
{
    // Two-pass layout solver.
    //
    // Pass 1 (post-order) — resolves intrinsic sizes: Pixels, Text (stub), ChildrenSum.
    // Pass 2 (pre-order)  — resolves extrinsic sizes: ParentPercent, Fill.
    //                       Then computes ScreenMin / ScreenMax for every box.
    //
    // After this call, every ZUIBox in the tree has valid ComputedSize, ScreenMin, ScreenMax.
    // Phase 3 will hook in real text measurement; until then, ZUISizeKind::Text resolves to 0.
    void ZUILayoutSolve(ZUIContext* ctx);

} // namespace ZEngine::UI

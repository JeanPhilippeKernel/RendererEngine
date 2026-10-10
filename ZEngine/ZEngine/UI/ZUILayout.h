#pragma once
#include <ZEngine/UI/ZUIContext.h>

namespace ZEngine::UI
{
    /// @brief Two-pass layout solver.
    ///
    /// Pass 1 (post-order) — resolves intrinsic sizes: Pixels, Text, ChildrenSum.
    /// Pass 2 (pre-order)  — resolves extrinsic sizes: ParentPercent, Fill;
    ///                       then computes ScreenMin / ScreenMax for every box.
    ///
    /// After this call every ZUIBox in the tree has valid ComputedSize, ScreenMin, ScreenMax.
    /// @param ctx Active ZUI context.
    void ZUILayoutSolve(ZUIContext* ctx);

} // namespace ZEngine::UI

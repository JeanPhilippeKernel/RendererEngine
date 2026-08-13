#pragma once
#include <cstdint>

namespace ZEngine::Rendering
{
    // Generational index handle. The Tag parameter produces distinct, incompatible types
    // at compile time — a BufferHandle cannot be passed where an ImageHandle is expected.
    // Generation 0 is the sentinel for an invalid handle; IsValid() is a single zero-check.
    // All specialisations are trivially copyable and fit in 8 bytes — pass by value everywhere.
    template <typename Tag>
    struct RenderHandle
    {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool     IsValid() const noexcept
        {
            return Generation != 0;
        }

        bool operator==(const RenderHandle&) const = default;
        bool operator!=(const RenderHandle&) const = default;
    };

    struct BufferTag
    {
    };
    struct ImageTag
    {
    };
    struct SamplerTag
    {
    };
    struct PipelineTag
    {
    };

    using BufferHandle   = RenderHandle<BufferTag>;
    using ImageHandle    = RenderHandle<ImageTag>;
    using SamplerHandle  = RenderHandle<SamplerTag>;
    using PipelineHandle = RenderHandle<PipelineTag>;

} // namespace ZEngine::Rendering

#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <ZEngine/Rendering/Specifications/ShaderSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct VertexInputBindingSpecification
    {
        uint32_t Stride  = 0;
        uint32_t Rate    = 0;
        uint32_t Binding = 0;
    };

    struct VertexInputAttributeSpecification
    {
        uint32_t    Location = 0;
        uint32_t    Binding  = 0;
        uint32_t    Offset   = 0;
        ImageFormat Format   = ImageFormat::UNDEFINED;
    };

    /// @brief Static, cacheable graphics pipeline state.
    /// @details The render graph supplies attachment compatibility. Per-frame
    /// viewport state, descriptors, and uniforms do not belong in this description.
    struct GraphicsPipelineDesc
    {
        bool                                                       EnableBlending                     = false;
        bool                                                       EnableDepthTest                    = false;
        bool                                                       EnableDepthWrite                   = true;
        bool                                                       EnableStencilTest                  = false;
        const char*                                                DebugName                          = {};
        uint32_t                                                   DepthCompareOp                     = VK_COMPARE_OP_LESS_OR_EQUAL;
        uint32_t                                                   CullMode                           = 0;
        ShaderSpecification                                        ShaderSpecificationValue           = {};
        Core::Containers::Array<VertexInputBindingSpecification>   VertexInputBindingSpecifications   = {};
        Core::Containers::Array<VertexInputAttributeSpecification> VertexInputAttributeSpecifications = {};
    };
    /// Clone exact admitted state without callback execution or asserting array
    /// growth. Failure preserves the output and exclusive arena transaction.
    [[nodiscard]] inline bool TryCloneGraphicsPipelineDesc(const GraphicsPipelineDesc& source, Core::Memory::ArenaAllocator* arena, GraphicsPipelineDesc* out)
    {
        if (!arena || !out)
        {
            return false;
        }
        Core::Memory::ArenaCheckpoint checkpoint(arena);
        GraphicsPipelineDesc          copy = {
            .EnableBlending           = source.EnableBlending,
            .EnableDepthTest          = source.EnableDepthTest,
            .EnableDepthWrite         = source.EnableDepthWrite,
            .EnableStencilTest        = source.EnableStencilTest,
            .DebugName                = source.DebugName,
            .DepthCompareOp           = source.DepthCompareOp,
            .CullMode                 = source.CullMode,
            .ShaderSpecificationValue = source.ShaderSpecificationValue,
        };
        const auto clone = [&](auto& destination, const auto& input) {
            destination.m_allocator = arena;
            if (!destination.try_reserve(input.size()))
            {
                return false;
            }
            for (const auto& element : input)
            {
                destination.push(element);
            }
            return true;
        };
        if (!clone(copy.VertexInputBindingSpecifications, source.VertexInputBindingSpecifications) || !clone(copy.VertexInputAttributeSpecifications, source.VertexInputAttributeSpecifications))
        {
            return false;
        }
        *out = std::move(copy);
        checkpoint.Commit();
        return true;
    }
} // namespace ZEngine::Rendering::Specifications

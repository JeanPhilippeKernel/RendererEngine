#pragma once
#include <Core/Containers/Array.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Specifications/ShaderSpecification.h>

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

    struct GraphicRendererPipelineSpecification
    {
        bool                                                       EnableBlending                     = false;
        bool                                                       EnableDepthTest                    = false;
        bool                                                       EnableDepthWrite                   = true;
        uint32_t                                                   DepthCompareOp                     = VK_COMPARE_OP_LESS_OR_EQUAL;
        bool                                                       EnableStencilTest                  = false;
        const char*                                                DebugName                          = {};
        ShaderSpecification                                        ShaderSpec                         = {};
        Rendering::Buffers::FramebufferVNext*                      TargetFrameBuffer                  = {};
        Renderers::RenderPasses::Attachment*                       Attachment                         = {};
        Core::Containers::Array<VertexInputBindingSpecification>   VertexInputBindingSpecifications   = {};
        Core::Containers::Array<VertexInputAttributeSpecification> VertexInputAttributeSpecifications = {};
    };
} // namespace ZEngine::Rendering::Specifications
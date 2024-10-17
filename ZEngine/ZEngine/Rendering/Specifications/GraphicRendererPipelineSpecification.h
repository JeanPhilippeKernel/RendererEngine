#pragma once
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <Rendering/Swapchain.h>
#include <ZEngineDef.h>

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
        bool                                           EnableBlending                     = false;
        bool                                           EnableDepthTest                    = false;
        bool                                           EnableDepthWrite                   = true;
        bool                                           EnableStencilTest                  = false;
        const char*                                    DebugName                          = {};
        ShaderSpecification                            ShaderSpecification                = {};
        Ref<Rendering::Buffers::FramebufferVNext>      TargetFrameBuffer                  = {};
        Ref<Rendering::Swapchain>                      SwapchainRenderTarget              = {};
        Ref<Renderers::RenderPasses::Attachment>       Attachment                         = {};
        std::vector<VertexInputBindingSpecification>   VertexInputBindingSpecifications   = {};
        std::vector<VertexInputAttributeSpecification> VertexInputAttributeSpecifications = {};
    };
} // namespace ZEngine::Rendering::Specifications
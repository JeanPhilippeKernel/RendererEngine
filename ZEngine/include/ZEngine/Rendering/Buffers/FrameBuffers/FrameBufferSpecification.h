#pragma once

namespace ZEngine::Rendering::Buffers {

    struct FrameBufferSpecification {
        unsigned int Height;
        unsigned int Width;
        bool HasDepth;
        bool HasStencil;
    };
   
}
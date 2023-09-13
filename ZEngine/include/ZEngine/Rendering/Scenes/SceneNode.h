#pragma once

namespace ZEngine::Rendering::Scenes
{
    struct NodeHierarchy
    {
        int Parent{-1};
        int FirstChild{-1};
        int RightSibling{-1};
        int DepthLevel{-1};
    };
}
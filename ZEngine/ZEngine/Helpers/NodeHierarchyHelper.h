#pragma once

namespace ZEngine::Helpers
{
    struct NodeHierarchy
    {
        int Parent       = -1; // -2 <-> node deleted
        int FirstChild   = -1;
        int LastChild    = -1;
        int RightSibling = -1;
        int DepthLevel   = -1;
    };
} // namespace ZEngine::Helpers
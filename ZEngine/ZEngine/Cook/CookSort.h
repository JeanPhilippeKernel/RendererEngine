#pragma once
#include <ZEngine/Core/Containers/HeapSort.h>

namespace ZEngine::Cook::Detail
{
    // One implementation: ECS can use Core without depending on the cooker.
    template <typename T>
    using CookSortState = Core::Containers::HeapSortState<T>;
    using Core::Containers::HeapMake;
    using Core::Containers::HeapPop;
    using Core::Containers::HeapPush;
    using Core::Containers::HeapSiftDown;
    using Core::Containers::HeapSort;
} // namespace ZEngine::Cook::Detail

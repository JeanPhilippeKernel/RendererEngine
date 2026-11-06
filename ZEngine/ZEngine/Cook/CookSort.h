#pragma once
#include <ZEngine/Core/Containers/HeapSort.h>

namespace ZEngine::Cook::Detail
{
    // One implementation: ECS can use Core without depending on the cooker.
    template <typename T>
    using CookSortState = Core::Containers::HeapSortState<T>;
} // namespace ZEngine::Cook::Detail

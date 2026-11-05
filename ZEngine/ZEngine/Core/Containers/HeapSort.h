#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <type_traits>
#include <utility>

namespace ZEngine::Core::Containers
{
    // Borrowed principal sorting state: the owner includes sizeof(HeapSortState<T>)
    // in its reservation. Iterative heapsort has no recursive/helper work stack,
    // no allocations and O(n log n) worst-case comparisons. Equal keys need not
    // retain input order; canonical callers must compare all relevant key fields.
    template <typename T>
    struct HeapSortState
    {
        static_assert(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_assignable_v<T>);
        T      Value     = {};
        size_t Root      = 0;
        size_t Child     = 0;
        size_t HeapCount = 0;
        size_t Start     = 0;
    };

    // Place the held value into a heap hole. Root/HeapCount and Value are set by
    // the caller; the same bounded sift is shared by sorting and queue removal.
    template <typename T, typename Before>
    void HeapSiftDown(ArrayView<T> values, HeapSortState<T>& state, const Before& before)
    {
        // Only parents enter the loop: 2 * Root + 1 cannot overflow size_t.
        while (state.Root < state.HeapCount / 2)
        {
            state.Child = state.Root * 2 + 1;
            if (state.Child + 1 < state.HeapCount && before(values[state.Child], values[state.Child + 1]))
            {
                ++state.Child;
            }
            if (!before(state.Value, values[state.Child]))
            {
                break;
            }
            values[state.Root] = std::move(values[state.Child]);
            state.Root         = state.Child;
        }
        values[state.Root] = std::move(state.Value);
    }

    // Bottom-up O(n) heap construction for a batch of initially ready nodes.
    template <typename T, typename Before>
    void HeapMake(ArrayView<T> values, HeapSortState<T>& state, const Before& before)
    {
        state.HeapCount = values.size();
        state.Start     = values.size() / 2;
        while (state.Start)
        {
            state.Root  = --state.Start;
            state.Value = std::move(values[state.Root]);
            HeapSiftDown(values, state, before);
        }
    }

    // Standard heap semantics: values.back() is newly appended for Push; Pop
    // moves the former root into values.back(). The comparator's greatest value
    // wins, so reverse the ordering for a minimum-first ready queue. The prefix
    // must already be a heap. No allocation, recursion, or state per tree level.
    template <typename T, typename Before>
    void HeapPush(ArrayView<T> values, HeapSortState<T>& state, const Before& before)
    {
        if (values.size() < 2)
        {
            return;
        }
        state.Root  = values.size() - 1;
        state.Value = std::move(values[state.Root]);
        while (state.Root)
        {
            state.Child = (state.Root - 1) / 2;
            if (!before(values[state.Child], state.Value))
            {
                break;
            }
            values[state.Root] = std::move(values[state.Child]);
            state.Root         = state.Child;
        }
        values[state.Root] = std::move(state.Value);
    }

    template <typename T, typename Before>
    void HeapPop(ArrayView<T> values, HeapSortState<T>& state, const Before& before)
    {
        if (values.size() < 2)
        {
            return;
        }
        state.HeapCount         = values.size() - 1;
        state.Value             = std::move(values[state.HeapCount]);
        values[state.HeapCount] = std::move(values[0]);
        state.Root              = 0;
        HeapSiftDown(values, state, before);
    }

    // A valid mutable span and a strict weak ordering are caller preconditions.
    // State is separate from the span and must not be borrowed by the comparator.
    // Move-only owners are supported without allocating or copying a record.
    template <typename T, typename Before>
    void HeapSort(ArrayView<T> values, HeapSortState<T>& state, const Before& before)
    {
        if (values.size() < 2)
        {
            return;
        }
        // Bottom-up heap construction, then extraction into the sorted suffix.
        // One held value fills the final hole instead of swapping large records
        // at every level. Queue operations share the exact same implementation.
        HeapMake(values, state, before);
        while (state.HeapCount > 1)
        {
            HeapPop<T>({values.data(), state.HeapCount}, state, before);
        }
    }
} // namespace ZEngine::Core::Containers

#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS
{
    using ComponentTypeID = uint32_t;

    namespace detail
    {
        inline uint32_t NextTypeID()
        {
            static PaddedAtomic<uint32_t> counter{};
            return counter.value.fetch_add(1, std::memory_order_acq_rel);
        }
    } // namespace detail

    template <typename T>
    ComponentTypeID ComponentTypeOf()
    {
        static const uint32_t id = detail::NextTypeID();
        return id;
    }
} // namespace ZEngine::ECS

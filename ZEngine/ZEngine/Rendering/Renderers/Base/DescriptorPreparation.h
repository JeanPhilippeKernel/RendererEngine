#pragma once
#include <ZEngine/Rendering/Specifications/ShaderSpecification.h>
#include <cstdint>
#include <span>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    /// Bounded proof of successful writes in one pass preparation, not a resource
    /// lifetime lease. Reflection and frame identity must still match at verification.
    class DescriptorPreparation
    {
    public:
        static constexpr uint32_t Capacity = 64;

        void                      Begin(uint32_t frame, uint32_t generation)
        {
            m_frame      = frame;
            m_generation = generation;
            m_count      = 0;
            m_failed     = false;
            m_active     = true;
        }

        void Reset()
        {
            m_active = false;
            m_count  = 0;
            m_failed = false;
        }

        bool Active() const
        {
            return m_active;
        }

        void Reject()
        {
            m_failed = true;
        }

        void Record(const Specifications::LayoutBindingSpecification& binding, uint32_t frame, uint32_t element, bool shared_array)
        {
            if (!m_active || frame != m_frame)
            {
                return; // Other buffered frames cannot satisfy this preparation.
            }
            if (!binding.Count || element >= binding.Count || (!shared_array && binding.Count > Capacity))
            {
                Reject();
                return;
            }
            uint32_t index = 0;
            while (index < m_count && (m_bindings[index].Specification.Set != binding.Set || m_bindings[index].Specification.Binding != binding.Binding))
            {
                ++index;
            }
            if (index == m_count)
            {
                if (m_count == Capacity)
                {
                    Reject();
                    return;
                }
                m_bindings[m_count++] = {.Specification = binding};
            }
            auto& entry = m_bindings[index];
            if (entry.Specification.Count != binding.Count || entry.Specification.DescriptorTypeValue != binding.DescriptorTypeValue)
            {
                Reject();
                return;
            }
            entry.SharedArray |= shared_array;
            if (!shared_array)
            {
                entry.Elements |= uint64_t(1) << element;
            }
        }

        bool Verify(std::span<const Specifications::LayoutBindingSpecification> reflection, uint32_t frame, uint32_t generation) const
        {
            if (!m_active || m_failed || frame != m_frame || generation != m_generation)
            {
                return false;
            }
            for (const auto& required : reflection)
            {
                bool complete = false;
                for (uint32_t index = 0; index < m_count; ++index)
                {
                    const auto& entry  = m_bindings[index];
                    const auto& actual = entry.Specification;
                    if (actual.Set != required.Set || actual.Binding != required.Binding || actual.Count != required.Count || actual.DescriptorTypeValue != required.DescriptorTypeValue || !required.Count)
                    {
                        continue;
                    }
                    const bool     shared   = entry.SharedArray && required.DescriptorTypeValue == Specifications::DescriptorType::SAMPLED_IMAGE;
                    const uint64_t expected = required.Count >= Capacity ? UINT64_MAX : (uint64_t(1) << required.Count) - 1;
                    complete                = shared || (required.Count <= Capacity && entry.Elements == expected);
                    break;
                }
                if (!complete)
                {
                    return false;
                }
            }
            return true;
        }

    private:
        struct Binding
        {
            Specifications::LayoutBindingSpecification Specification = {};
            uint64_t                                   Elements      = 0;
            bool                                       SharedArray   = false;
        };
        Binding  m_bindings[Capacity] = {};
        uint32_t m_count              = 0;
        uint32_t m_frame              = UINT32_MAX;
        uint32_t m_generation         = UINT32_MAX;
        bool     m_active             = false;
        bool     m_failed             = false;
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses

#pragma once
#include <Rendering/Specifications/FrameBufferSpecification.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext : public Helpers::RefCounted
    {
        FramebufferVNext() = default;
        FramebufferVNext(const Specifications::FrameBufferSpecificationVNext&);
        FramebufferVNext(Specifications::FrameBufferSpecificationVNext&&);
        ~FramebufferVNext();

        void                                                 Create();
        void                                                 Resize(uint32_t width = 1, uint32_t height = 1);
        void                                                 Dispose();
        VkFramebuffer                                        GetHandle() const;
        uint32_t                                             GetWidth() const;
        uint32_t                                             GetHeight() const;
        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;

        static Ref<FramebufferVNext> Create(const Specifications::FrameBufferSpecificationVNext&);
        static Ref<FramebufferVNext> Create(Specifications::FrameBufferSpecificationVNext&&);

    private:
        VkFramebuffer                                 m_handle{VK_NULL_HANDLE};
        Specifications::FrameBufferSpecificationVNext m_specification{};
    };
} // namespace ZEngine::Rendering::Buffers

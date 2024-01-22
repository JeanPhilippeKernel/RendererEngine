#include <pch.h>
#include <Rendering/Buffers/Framebuffer.h>

using namespace  ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(const Specifications::FrameBufferSpecificationVNext& specification) : m_specification(specification)
    {
        Create();
    }

    FramebufferVNext::FramebufferVNext(Specifications::FrameBufferSpecificationVNext&& specification) : m_specification(std::move(specification))
    {
        Create();
    }

    FramebufferVNext::~FramebufferVNext()
    {
        Dispose();
    }

    Ref<FramebufferVNext> FramebufferVNext::Create(const Specifications::FrameBufferSpecificationVNext& spec)
    {
        return CreateRef<FramebufferVNext>(spec);
    }

    Ref<FramebufferVNext> FramebufferVNext::Create(Specifications::FrameBufferSpecificationVNext&& spec)
    {
        return CreateRef<FramebufferVNext>(std::move(spec));
    }

    VkFramebuffer FramebufferVNext::GetHandle() const
    {
        return m_handle;
    }

    uint32_t FramebufferVNext::GetWidth() const
    {
        return m_specification.Width;
    }

    uint32_t FramebufferVNext::GetHeight() const
    {
        return m_specification.Height;
    }

    FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification()
    {
        return m_specification;
    }

    const FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification() const
    {
        return m_specification;
    }

    void FramebufferVNext::Create()
    {
        m_handle = Hardwares::VulkanDevice::CreateFramebuffer(
            m_specification.RenderTargetViews, m_specification.Attachment->GetHandle(), m_specification.Width, m_specification.Height, m_specification.Layers);
    }

    void FramebufferVNext::Resize(uint32_t width, uint32_t height)
    {
        m_specification.Width  = width;
        m_specification.Height = height;
        Dispose();
        Create();
    }

    void FramebufferVNext::Dispose()
    {
        if (m_handle)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::FRAMEBUFFER, m_handle);
            m_handle = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Buffers

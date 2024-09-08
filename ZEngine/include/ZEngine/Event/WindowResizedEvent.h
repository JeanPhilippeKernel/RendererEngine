#pragma once
#include <Event/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event
{
    class WindowResizedEvent : public CoreEvent
    {
    public:
        WindowResizedEvent(unsigned int width, unsigned int height) : m_width(width), m_height(height)
        {
            m_name = "WindowResized";
        }

        ~WindowResizedEvent() = default;

        unsigned int GetWidth() const
        {
            return m_width;
        }
        unsigned int GetHeight() const
        {
            return m_height;
        }

        void SetWidth(unsigned int value)
        {
            m_width = value;
        }

        void SetHeight(unsigned int value)
        {
            m_height = value;
        }

        EventType GetType() const override
        {
            return GetStaticType();
        }

        int GetCategory() const override
        {
            return GetStaticCategory();
        }

        std::string ToString() const override
        {
            return fmt::format("WindowResizeEvent X: {0}, Y: {1}", m_width, m_height);
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowResized)

    private:
        unsigned int m_width{0};
        unsigned int m_height{0};
    };
} // namespace ZEngine::Event

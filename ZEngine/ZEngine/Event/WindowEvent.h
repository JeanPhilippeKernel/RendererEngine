#pragma once
#include <Event/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event
{
    class WindowRestoredEvent : public CoreEvent
    {
    public:
        WindowRestoredEvent()
        {
            m_name = "WindowRestored";
        }
        ~WindowRestoredEvent() = default;

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
            return fmt::format("WindowRestoredEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowRestored)
    };

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

    class WindowMinimizedEvent : public CoreEvent
    {
    public:
        WindowMinimizedEvent()
        {
            m_name = "WindowMinimized";
        }
        ~WindowMinimizedEvent() = default;

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
            return fmt::format("WindowMinimizedEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowMinimized)
    };

    class WindowMaximizedEvent : public CoreEvent
    {
    public:
        WindowMaximizedEvent()
        {
            m_name = "WindowMaximized";
        }
        ~WindowMaximizedEvent() = default;

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
            return fmt::format("WindowMaximizedEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowMaximized)
    };

    class WindowClosedEvent : public CoreEvent
    {
    public:
        WindowClosedEvent()
        {
            m_name = "WindowClosed";
        }
        ~WindowClosedEvent() = default;

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
            return fmt::format("WindowClosedEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowClosed)
    };
} // namespace ZEngine::Event

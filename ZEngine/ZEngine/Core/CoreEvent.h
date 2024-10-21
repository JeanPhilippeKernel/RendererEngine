#pragma once
#include <ZEngineDef.h>
#include <string>

#define EVENT_TYPE(value)                           \
    static ZEngine::Core::EventType GetStaticType() \
    {                                               \
        return ZEngine::Core::EventType::value;     \
    }

#define EVENT_CATEGORY(value)                                             \
    static uint8_t GetStaticCategory()                                    \
    {                                                                     \
        return static_cast<uint8_t>(ZEngine::Core::EventCategory::value); \
    }

namespace ZEngine::Core
{
    enum EventCategory : uint8_t
    {
        None                   = 0,
        Engine                 = BIT(0),
        Keyboard               = BIT(1),
        Mouse                  = BIT(2),
        Input                  = BIT(3),
        UserInterfaceComponent = BIT(4)
    };

    enum class EventType : uint8_t
    {
        None = 0,

        WindowShown,
        WindowHidden,
        WindowMoved,
        WindowResized,
        WindowClosed,
        WindowSizeChanged,
        WindowMinimized,
        WindowMaximized,
        WindowRestored,
        WindowFocusLost,
        WindowFocusGained,

        KeyPressed,
        KeyReleased,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseWheel,

        EngineClosed,

        TextInput,

        UserInterfaceComponent,
        SceneViewportResized,
        SceneViewportFocused,
        SceneViewportUnfocused,
        SceneTextureAvailable
    };

    class CoreEvent
    {
    public:
        CoreEvent()          = default;
        virtual ~CoreEvent() = default;

        void SetHandled(bool value)
        {
            m_handled = value;
        }

        bool IsHandled() const
        {
            return m_handled;
        }

        std::string_view GetName() const
        {
            return m_name;
        }

        void SetName(std::string_view value)
        {
            m_name = value;
        }

        virtual EventType   GetType() const     = 0;
        virtual int         GetCategory() const = 0;
        virtual std::string ToString() const    = 0;

    protected:
        bool        m_handled{false};
        std::string m_name{};
    };
} // namespace ZEngine::Core

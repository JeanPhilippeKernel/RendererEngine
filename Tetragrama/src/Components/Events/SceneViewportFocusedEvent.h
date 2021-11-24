#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components::Event {

    class SceneViewportFocusedEvent : public ZEngine::Components::UI::Event::UIComponentEvent {
    public:
        SceneViewportFocusedEvent()  = default;
        ~SceneViewportFocusedEvent() = default;

        EVENT_TYPE(SceneViewportFocused)

        virtual ZEngine::Event::EventType GetType() const override {
            return GetStaticType();
        }

        virtual int GetCategory() const override {
            return GetStaticCategory();
        }
    };
} // namespace Tetragrama::Components::Event

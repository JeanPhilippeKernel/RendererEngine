#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components::Event {

    class SceneTextureAvailableEvent : public ZEngine::Components::UI::Event::UIComponentEvent {
    public:
        SceneTextureAvailableEvent(uint32_t texture_identifier)
            : m_texture_identifier(texture_identifier)
        {}

        EVENT_TYPE(SceneTextureAvailable)

        uint32_t GetSceneTexture() const { return m_texture_identifier; }

		virtual ZEngine::Event::EventType GetType() const override {
			return GetStaticType();
		}

		virtual int GetCategory() const override {
			return GetStaticCategory();
		}

    private:
        uint32_t m_texture_identifier;
    };
}
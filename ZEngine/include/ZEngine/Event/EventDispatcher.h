#pragma once
#include <functional>
#include <ZEngineDef.h>
#include <Event/CoreEvent.h>


namespace ZEngine::Event {
	class EventDispatcher {
	public:
		template<typename T, typename = std::enable_if_t<std::is_base_of_v<CoreEvent, T>>>
		using EventFn = std::function<bool(T&)>;

		template<typename T, typename = std::enable_if_t<std::is_base_of_v<CoreEvent, T>>>
		using ForwardEventFn = std::function<void(T&)>;

	public:
		EventDispatcher(CoreEvent& event)
			:m_event(event)
		{}

		template<typename K>
		bool Dispatch(const EventFn<K>& func) {

			if (m_event.GetType() == K::GetStaticType()) {
				m_event.SetHandled(func(dynamic_cast<K&>(m_event)));
				return true;
			}

			return false;
		}

		template<typename K>
		void ForwardTo(const ForwardEventFn<K>& func) {
			if (m_event.GetType() == K::GetStaticType()) {
				func(dynamic_cast<K&>(m_event));
			}
		}

	private:
		CoreEvent& m_event;
	};
}
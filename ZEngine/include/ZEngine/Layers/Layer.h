#pragma once
#include <string>
#include <algorithm>
#include <vector>
#include <Event/CoreEvent.h>
#include <ZEngineDef.h>
#include <Core/TimeStep.h>
#include <Window/CoreWindow.h>

#include <Core/IInitializable.h>
#include <Core/IEventable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>

namespace ZEngine::Window {
    class CoreWindow;
}

namespace ZEngine::Layers {

    struct LayerInformation {
        std::string Sender;
        void*       Data{nullptr};
    };

    class Layer : public Core::IInitializable, public Core::IUpdatable, public Core::IEventable, public Core::IRenderable {

    public:
        Layer(const char* name = "default_layer") : m_name(name) {
            m_information.Sender = name;
        }

        virtual ~Layer() = default;

        std::string_view GetName() const {
            return m_name;
        }

        void SetAttachedWindow(const ZEngine::Ref<Window::CoreWindow>& window) {
            m_window = window;
        }

        ZEngine::Ref<ZEngine::Window::CoreWindow> GetAttachedWindow() const {
            if (!m_window.expired())
                return m_window.lock();

            return nullptr;
        }

        const LayerInformation& GetLayerInformation() const {
            return m_information;
        }

        void AddSubscriber(const Ref<Layer>& subscriber) {
            m_subscribers.push_back(subscriber);
        }

        virtual void OnRenderCallback() {
            if (m_information.Data) {
                std::for_each(std::begin(m_subscribers), std::end(m_subscribers), [this](Ref<Layer>& subscriber) { subscriber->OnRecievedData(this, m_information); });
            }
        }

        virtual void OnRecievedData(const void*, const LayerInformation&) {}

    protected:
        std::string                                   m_name;
        LayerInformation                              m_information;
        ZEngine::WeakRef<ZEngine::Window::CoreWindow> m_window;
        std::vector<Ref<Layer>>                       m_subscribers;
    };
} // namespace ZEngine::Layers

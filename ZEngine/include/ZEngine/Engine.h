#pragma once

#include <ZEngineDef.h>
#include <Window/CoreWindow.h>
#include <Event/EventDispatcher.h>
#include <Event/EngineClosedEvent.h>
#include <Event/KeyPressedEvent.h>
#include <Event/KeyReleasedEvent.h>
#include <Core/TimeStep.h>
#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <EngineConfiguration.h>

#include <Hardwares/VulkanInstance.h>

namespace ZEngine
{
    class Engine : public Core::IInitializable, public Core::IUpdatable, public Core::IRenderable
    {

    public:
        explicit Engine(const EngineConfiguration&);
        virtual ~Engine();

    public:
        virtual void Initialize() override;
        virtual void Update(Core::TimeStep delta_time) override;
        virtual void Render() override;

    public:
        void Start();

        const Ref<ZEngine::Window::CoreWindow>& GetWindow() const
        {
            return m_window;
        }

        static Core::TimeStep GetDeltaTime()
        {
            return m_delta_time;
        }

        Hardwares::VulkanInstance& GetVulkanInstance();

    protected:
        void         Run();
        virtual void ProcessEvent();

        virtual void Deinitialize() override;

    public:
        bool OnEngineClosed(Event::EngineClosedEvent&);

    private:
        bool                             m_request_terminate;
        float                            m_last_frame_time;
        Ref<ZEngine::Window::CoreWindow> m_window;
        Hardwares::VulkanInstance        m_vulkan_instance;
        const EngineConfiguration&       m_engine_configuration;
        static Core::TimeStep            m_delta_time;
    };

    Engine* CreateEngine(const EngineConfiguration&);
} // namespace ZEngine

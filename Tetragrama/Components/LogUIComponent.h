#pragma once
#include <UIComponent.h>
#include <ZEngine/Logging/Logger.h>
#include <string>

namespace Tetragrama::Components
{
    class LogUIComponent : public UIComponent
    {
    public:
        LogUIComponent(std::string_view name = "Console", bool visibility = true);
        virtual ~LogUIComponent();

        virtual void Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer) override;

        void         OnLog(ZEngine::Logging::LogMessage);

        void         ClearLog();

        std::string  GetMessageType(const ZEngine::Logging::LogMessage& message);

    private:
        uint32_t                                  m_maxCount{1024};
        uint32_t                                  m_currentCount{0};
        bool                                      m_auto_scroll{true};
        bool                                      m_is_copy_button_pressed{false};
        std::vector<ZEngine::Logging::LogMessage> m_log_queue;
        std::mutex                                m_mutex;
        char                                      m_search_buffer[256] = "";
    };
} // namespace Tetragrama::Components

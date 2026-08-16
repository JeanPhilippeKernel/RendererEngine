#pragma once
#include <Tetragrama/Components/UIComponent.h>

namespace Tetragrama::Components
{
    class StatusBarUIComponent : public UIComponent
    {
    public:
        StatusBarUIComponent()           = default;
        ~StatusBarUIComponent() override = default;

        void         Initialize(Layers::ImguiLayer* parent = nullptr, cstring name = "##StatusBar", bool visibility = true, bool closed = false) override;
        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        static constexpr int kFtSamples                = 30;
        float                m_frame_times[kFtSamples] = {};
        int                  m_ft_head                 = 0;
        float                m_smoothed_dt             = 0.016f;
    };
} // namespace Tetragrama::Components

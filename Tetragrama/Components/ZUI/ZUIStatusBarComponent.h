#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>

namespace Tetragrama::Components
{
    class ZUIStatusBarComponent : public ZUIComponent
    {
    public:
        ZUIStatusBarComponent()           = default;
        ~ZUIStatusBarComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "StatusBar", bool visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        static constexpr int   kFtSamples                = 32;
        static constexpr float kBarH                     = 28.f;

        float                  m_frame_times[kFtSamples] = {};
        int                    m_ft_head                 = 0;
        float                  m_smoothed_dt             = 0.f;
    };
    ZDEFINE_PTR(ZUIStatusBarComponent);
} // namespace Tetragrama::Components

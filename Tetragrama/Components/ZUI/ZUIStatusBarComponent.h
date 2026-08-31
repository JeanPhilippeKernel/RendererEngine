#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Components
{
    /// @brief Bottom status bar showing panel toggles, scene name, selected actor,
    ///        camera position, and a smoothed FPS/ms counter.  Floated to the
    ///        bottom of the screen or to the region assigned by the dockspace.
    class ZUIStatusBarComponent : public ZUIComponent
    {
    public:
        ZUIStatusBarComponent()           = default;
        ~ZUIStatusBarComponent() override = default;

        /// @brief Stores parent/name/visibility.
        /// @param parent     Owning ZUI layer.
        /// @param name       Component name.
        /// @param visibility Initial visibility.
        void                          Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "StatusBar", bool visibility = true) override;

        /// @brief Builds the status bar row for this frame.
        /// @param ctx ZUI context for the current frame.
        void                          BuildUI(ZEngine::UI::ZUIContext* ctx) override;

        /// @brief Panel manager used to toggle panel visibility from the status bar buttons.
        ZEngine::UI::ZUIPanelManager* ShellPanelManager = nullptr;

    private:
        static constexpr int   kFtSamples                = 32;
        static constexpr float kBarH                     = 28.f;

        float                  m_frame_times[kFtSamples] = {};
        int                    m_ft_head                 = 0;
        int                    m_ft_count                = 0; // samples actually written; < kFtSamples during warmup
        float                  m_smoothed_dt             = 0.f;
    };
    ZDEFINE_PTR(ZUIStatusBarComponent);
} // namespace Tetragrama::Components

#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>

namespace Tetragrama::Components
{
    class ZUIInspectorViewComponent : public ZUIComponent
    {
    public:
        ZUIInspectorViewComponent()          = default;
        ~ZUIInspectorViewComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Inspector",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        bool m_transform_open = true;
        bool m_mesh_open      = true;
    };
    ZDEFINE_PTR(ZUIInspectorViewComponent);
} // namespace Tetragrama::Components

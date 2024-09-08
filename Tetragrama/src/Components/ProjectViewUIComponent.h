#pragma once
#include <ZEngine/ZEngine.h>
#include <string>

namespace Tetragrama::Components
{
    class ProjectViewUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        ProjectViewUIComponent(std::string_view name = "Project", bool visibility = true);
        virtual ~ProjectViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;
    };
} // namespace Tetragrama::Components

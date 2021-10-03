#pragma once 
#include <string>
#include <Core/IRenderable.h>

namespace ZEngine::Components::UI
{
    class UIComponent : public Core::IRenderable  {
    
    public:
        UIComponent() = default;
        UIComponent(std::string_view name, bool visibility);
        virtual ~UIComponent() = default;

        void SetName(std::string_view name);
        void SetVisibility(bool visibility);

        std::string_view GetName() const;
        bool GetVisibility() const;

    protected:
        std::string m_name;
        bool m_visibility{false};
    };
}
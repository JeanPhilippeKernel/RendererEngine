#pragma once 
#include <string>
#include <Core/IRenderable.h>
#include <Layers/ImguiLayer.h>
#include <ZEngineDef.h>
#include <ZEngine/Components/UIComponentEvent.h>

namespace ZEngine::Layers { class ImguiLayer; }

namespace ZEngine::Components::UI
{
    class UIComponent : public Core::IRenderable  {
    
    public:
        UIComponent() = default;
        UIComponent(std::string_view name, bool visibility);
        UIComponent(const Ref<Layers::ImguiLayer>& layer, std::string_view name, bool visibility);
        virtual ~UIComponent() = default;

        void SetName(std::string_view name);
        void SetVisibility(bool visibility);

        std::string_view GetName() const;
        bool GetVisibility() const;

        void SetParentLayer(const Ref<Layers::ImguiLayer>& layer);
        bool HasParentLayer();

    protected:
        bool m_visibility{ false };
        std::string m_name;
        WeakRef<Layers::ImguiLayer> m_parent_layer;

        virtual bool OnUIComponentRaised(Event::UIComponentEvent&) = 0;
    };
}
#pragma once 
#include <string>
#include <vector>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Layers/ImguiLayer.h>
#include <ZEngineDef.h>
#include <ZEngine/Components/UIComponentEvent.h>

namespace ZEngine::Layers { class ImguiLayer; }

namespace ZEngine::Components::UI
{
    class UIComponent : 
    public Core::IRenderable,
    public Core::IUpdatable,
    public std::enable_shared_from_this<UIComponent>  {
    
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
        void SetParentUI(const Ref<UIComponent>& item);

        bool HasParentLayer() const;
        bool HasParentUI() const;
        bool HasChildren() const;

        void AddChild(Ref<UIComponent>& item);
        void AddChild(Ref<UIComponent>&& item);
        void AddChild(const Ref<UIComponent>& item);        

        void AddChildren(std::vector<Ref<UIComponent>>& items);
        void AddChildren(std::vector<Ref<UIComponent>>&& items);        

    protected:
        bool m_visibility{ false };
        std::string m_name;
        WeakRef<Layers::ImguiLayer> m_parent_layer;
        WeakRef<UIComponent> m_parent_ui;
        std::vector<Ref<UIComponent>> m_children;

        virtual bool OnUIComponentRaised(Event::UIComponentEvent&) = 0;
    };
}
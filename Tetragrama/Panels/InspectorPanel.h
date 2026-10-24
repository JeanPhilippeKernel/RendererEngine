#pragma once
#include <Tetragrama/EditorTarget.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/ECS/Reflection/ComponentMeta.h>
#include <ZEngine/UI/ZUIIcons.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <array>

namespace Tetragrama
{
    class EditorSession;
    struct EditorControlDrag;
} // namespace Tetragrama

namespace Tetragrama::Panels
{
    // Inspector panel
    //
    // Reflection-driven: iterates ComponentReflectionRegistry::ForEach for the
    // selected actor, draws every registered component with ZUI widgets.
    // Matches develop InspectorViewUIComponent architecture translated to ZUI.

    /// @brief Reflection-driven component inspector panel.  For the selected actor
    ///        it iterates ComponentReflectionRegistry and renders every registered
    ///        component section with collapsible headers, field widgets, and a
    ///        category pill filter row.
    struct InspectorPanel : ZEngine::UI::ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Details";
            Icon  = (int) ZEngine::UI::ZUIIcon::SlidersHorizontal;
        }

        Tetragrama::Layers::ZUILayer* m_layer        = nullptr;
        char                          m_search[128]  = {};
        char                          m_category[64] = "All"; // active category pill ("All" = no filter)
        bool                          m_sec_open[64] = {};    // per TypeID, initialized to true
        bool                          m_sec_init     = false;
        bool                          m_renaming     = false; // actor name is being edited in place

        /// @brief Builds the inspector UI for the currently selected actor.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        friend struct InspectorPanelTestAccess;
        void                 BeginNameEdit(ZEngine::UI::ZUIContext* ctx, EditorSession& session, ZEngine::ECS::EntityID entity, uint64_t key);
        void                 FinishNameEdit(ZEngine::UI::ZUIContext* ctx, EditorSession& session, bool commit);
        void                 DrawNameEdit(ZEngine::UI::ZUIContext* ctx, EditorSession& session, ZEngine::ECS::EntityID entity, const char* key, float width);
        static bool          DrawField(ZEngine::UI::ZUIContext* ctx, const ZEngine::ECS::FieldDescriptor& field, void* component, float panel_width, uint32_t component_index, uint32_t field_index, EditorSession* session = nullptr, ZEngine::ECS::EntityID entity = {}, const EditorControlDrag* control = nullptr, uintptr_t color_binding = 0);
        static bool          DrawSessionField(ZEngine::UI::ZUIContext* ctx, const ZEngine::ECS::ComponentMeta& meta, const ZEngine::ECS::FieldDescriptor& field, EditorSession& session, ZEngine::ECS::EntityID entity, float panel_width, uint32_t component_index, uint32_t field_index);
        bool                 DrawUntrackedField(ZEngine::UI::ZUIContext* ctx, const ZEngine::ECS::ComponentMeta& meta, const ZEngine::ECS::FieldDescriptor& field, EditorSession& session, ZEngine::ECS::EntityID entity, float panel_width, uint32_t component_index, uint32_t field_index);

        EntityTarget         m_name_target        = {};
        uint64_t             m_name_key           = 0;
        uint64_t             m_name_drawn_frame   = 0;
        char                 m_name_buffer[128]   = {};
        char                 m_name_original[128] = {};
        EntityTarget         m_color_target       = {};
        uint64_t             m_color_key          = 0;
        uintptr_t            m_color_binding      = 0;
        std::array<float, 3> m_color_value        = {1.f, 1.f, 1.f};

        struct PropertyBinding
        {
            EntityTarget Target     = {};
            uint64_t     ControlKey = 0;
            uint64_t     PopupKey   = 0;
        };
        std::array<PropertyBinding, 2> m_property_bindings = {};
    };
} // namespace Tetragrama::Panels

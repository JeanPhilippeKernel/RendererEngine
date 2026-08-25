#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/EntityID.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    class HierarchyViewUIComponent : public UIComponent
    {
    public:
        HierarchyViewUIComponent();
        virtual ~HierarchyViewUIComponent();

        void         Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Hierarchy", bool visibility = true, bool closed = false) override;
        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        void                                                     RenderGuizmo(EditorPtr app, EditorScenePtr scene);

        static void                                              DrawArrow(ImDrawList* dl, ImVec2 pos, float row_h, bool expanded, ImU32 color);
        static void                                              DrawTypeIcon(ImDrawList* dl, ImVec2 pos, float sz, const char* type, bool is_collection);

        int                                                      m_gizmo_operation{-1};
        char                                                     m_filter_buf[128]      = {};

        ZEngine::ECS::ActorHandle                                m_renaming_handle      = {};
        char                                                     m_rename_buf[128]      = {};

        // Collapsed entity IDs — everything NOT in this list is expanded by default.
        ZEngine::Core::Memory::ArenaAllocator                    m_outliner_arena       = {};
        ZEngine::Core::Containers::Array<ZEngine::ECS::EntityID> m_collapsed            = {};
        bool                                                     m_scene_root_collapsed = false;

        bool                                                     IsCollapsed(ZEngine::ECS::EntityID eid) const;
        void                                                     ToggleCollapsed(ZEngine::ECS::EntityID eid);
    };
} // namespace Tetragrama::Components

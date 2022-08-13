#pragma once
#include <string_view>

namespace Tetragrama {
    static std::string_view EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT = "editor::component::dockspace::request::exit";

    static std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_TEXTURE_AVAILABLE     = "editor::component::sceneviewport::texture_available";
    static std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_UNFOCUSED             = "editor::component::sceneviewport::unfocused";
    static std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED               = "editor::component::sceneviewport::focused";
    static std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_RESIZED               = "editor::component::sceneviewport::resized";
    static std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_REQUEST_RECOMPUTATION = "editor::component::sceneviewport::request::recomputation";

    static std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED                  = "editor::component::hierarchyview::node_selected";
    static std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED                = "editor::component::hierarchyview::node_unselected";
    static std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED                   = "editor::component::hierarchyview::node_deleted";
    static std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_REQUEST_RESUME_OR_PAUSE_RENDER = "editor::component::hierarchyview::request::resume_or_pause_render";

    static std::string_view EDITOR_COMPONENT_INSPECTORVIEW_REQUEST_RESUME_OR_PAUSE_RENDER = "editor::component::inspectorview::request::resume_or_pause_render";

    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE          = "editor::render_layer::scene::request::resize";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS           = "editor::render_layer::scene::request::focus";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS         = "editor::render_layer::scene::request::unfocus";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_AVAILABLE               = "editor::render_layer::scene::available";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION   = "editor::render_layer::scene::request::serialization";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION = "editor::render_layer::scene::request::deserialization";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE        = "editor::render_layer::scene::request::newscene";
    static std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE       = "editor::render_layer::scene::request::openscene";

    static std::string_view EDITOR_COMPONENT_LOG_RECEIVE_LOG_MESSAGE = "editor::component::log::receive_log_message";
} // namespace Tetragrama

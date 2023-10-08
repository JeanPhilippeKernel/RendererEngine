#pragma once
#include <string_view>

namespace Tetragrama
{
    static const std::string_view EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT                       = "editor::component::dockspace::request::exit";
    static const std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_UNFOCUSED                      = "editor::component::sceneviewport::unfocused";
    static const std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED                        = "editor::component::sceneviewport::focused";
    static const std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_RESIZED                        = "editor::component::sceneviewport::resized";
    static const std::string_view EDITOR_COMPONENT_SCENEVIEWPORT_CLICKED                        = "editor::component::sceneviewport::clicked";
    static const std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED                  = "editor::component::hierarchyview::node_selected";
    static const std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED                = "editor::component::hierarchyview::node_unselected";
    static const std::string_view EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED                   = "editor::component::hierarchyview::node_deleted";
    static const std::string_view EDITOR_COMPONENT_INSPECTORVIEW_REQUEST_RESUME_OR_PAUSE_RENDER = "editor::component::inspectorview::request::resume_or_pause_render";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE                      = "editor::render_layer::scene::request::resize";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS                       = "editor::render_layer::scene::request::focus";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL    = "editor::render_layer::scene::request::select_entity_from_pixel";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS                     = "editor::render_layer::scene::request::unfocus";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_AVAILABLE                           = "editor::render_layer::scene::available";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION               = "editor::render_layer::scene::request::serialization";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION             = "editor::render_layer::scene::request::deserialization";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE                    = "editor::render_layer::scene::request::newscene";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE                   = "editor::render_layer::scene::request::openscene";
    static const std::string_view EDITOR_RENDER_LAYER_SCENE_REQUEST_IMPORTASSETMODEL            = "editor::render_layer::scene::request::importassetmodel";
    static const std::string_view EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE               = "editor::render_layer::scene::camera_controller_available";
    static const std::string_view EDITOR_COMPONENT_LOG_RECEIVE_LOG_MESSAGE                      = "editor::component::log::receive_log_message";
} // namespace Tetragrama

# Libraries
#

# zEngineLib
get_target_property(ZENGINE_INTERFACE_INCLUDE_DIRS zEngineLib INTERFACE_INCLUDE_DIRECTORIES)

add_library (imported::zEngineLib INTERFACE IMPORTED)
set_target_properties(imported::zEngineLib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ZENGINE_INTERFACE_INCLUDE_DIRS}")
target_link_libraries(imported::zEngineLib INTERFACE zEngineLib)
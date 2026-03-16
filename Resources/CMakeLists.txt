set(ShaderPath ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)
set(ShaderOutputPath "${ShaderPath}/Cache")

set(Flags)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Flags "-g")
else()
  set(Flags "-Os")
endif()

file (MAKE_DIRECTORY "${ShaderOutputPath}")

file (GLOB VertexShaders CONFIGURE_DEPENDS "${ShaderPath}/*.vert")
file (GLOB FragmentShaders CONFIGURE_DEPENDS "${ShaderPath}/*.frag")

foreach(VertexShader ${VertexShaders})

      cmake_path(GET VertexShader STEM shaderfilename)
      cmake_path(APPEND_STRING shaderfilename "_vertex.spv" OUTPUT_VARIABLE VertexShaderOutputFileName )
      cmake_path(APPEND ShaderOutputPath ${VertexShaderOutputFileName} OUTPUT_VARIABLE VertexShaderOutputPath)

      add_custom_target("Compile-${shaderfilename}-vertex" ALL COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags} "-o" "${VertexShaderOutputPath}" "${VertexShader}")

    
endforeach()

foreach(FragmentShader ${FragmentShaders})

    cmake_path(GET FragmentShader STEM shaderfilename)
    cmake_path(APPEND_STRING shaderfilename "_fragment.spv" OUTPUT_VARIABLE FragmentShaderOutputFileName )
    cmake_path(APPEND ShaderOutputPath ${FragmentShaderOutputFileName} OUTPUT_VARIABLE FragmentShaderOutputPath)


    add_custom_target("Compile-${shaderfilename}-fragment" ALL COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags}  "-o" "${FragmentShaderOutputPath}" "${FragmentShader}")

endforeach()

install(DIRECTORY Editor/Settings DESTINATION bin)
install(DIRECTORY Shaders/Cache DESTINATION bin/Shaders)

# This helps for debugging and development
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  install(DIRECTORY Editor/Settings DESTINATION Obelisk/${CMAKE_BUILD_TYPE})
  install(DIRECTORY Shaders/Cache DESTINATION Obelisk/${CMAKE_BUILD_TYPE}/Shaders)
endif()
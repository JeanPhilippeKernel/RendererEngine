set(ShaderPath ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)
set(ShaderOutputPath "${ShaderPath}/Cache")

set(Flags)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Flags "-g")
else()
  set(Flags "-Os")
endif()

file (MAKE_DIRECTORY "${ShaderOutputPath}")

# Mirror locations that the engine binary reads at runtime.
set(ShaderBinOutputPath "${CMAKE_INSTALL_PREFIX}/bin/Shaders/Cache")
if (UNIX AND NOT APPLE)
  set(ShaderObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/Shaders/Cache")
else()
  set(ShaderObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/${CMAKE_BUILD_TYPE}/Shaders/Cache")
endif()

file (MAKE_DIRECTORY "${ShaderBinOutputPath}")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  file (MAKE_DIRECTORY "${ShaderObeliskOutputPath}")
endif()

file (GLOB VertexShaders CONFIGURE_DEPENDS "${ShaderPath}/*.vert")
file (GLOB FragmentShaders CONFIGURE_DEPENDS "${ShaderPath}/*.frag")

foreach(VertexShader ${VertexShaders})

      cmake_path(GET VertexShader STEM shaderfilename)
      cmake_path(APPEND_STRING shaderfilename "_vertex.spv" OUTPUT_VARIABLE VertexShaderOutputFileName)
      cmake_path(APPEND ShaderOutputPath ${VertexShaderOutputFileName} OUTPUT_VARIABLE VertexShaderOutputPath)

      if (CMAKE_BUILD_TYPE STREQUAL "Debug")
          add_custom_target("Compile-${shaderfilename}-vertex" ALL
              COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags} "-o" "${VertexShaderOutputPath}" "${VertexShader}"
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VertexShaderOutputPath}" "${ShaderBinOutputPath}/${VertexShaderOutputFileName}"
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VertexShaderOutputPath}" "${ShaderObeliskOutputPath}/${VertexShaderOutputFileName}")
      else()
          add_custom_target("Compile-${shaderfilename}-vertex" ALL
              COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags} "-o" "${VertexShaderOutputPath}" "${VertexShader}"
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VertexShaderOutputPath}" "${ShaderBinOutputPath}/${VertexShaderOutputFileName}")
      endif()

endforeach()

foreach(FragmentShader ${FragmentShaders})

    cmake_path(GET FragmentShader STEM shaderfilename)
    cmake_path(APPEND_STRING shaderfilename "_fragment.spv" OUTPUT_VARIABLE FragmentShaderOutputFileName)
    cmake_path(APPEND ShaderOutputPath ${FragmentShaderOutputFileName} OUTPUT_VARIABLE FragmentShaderOutputPath)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_target("Compile-${shaderfilename}-fragment" ALL
            COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags} "-o" "${FragmentShaderOutputPath}" "${FragmentShader}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FragmentShaderOutputPath}" "${ShaderBinOutputPath}/${FragmentShaderOutputFileName}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FragmentShaderOutputPath}" "${ShaderObeliskOutputPath}/${FragmentShaderOutputFileName}")
    else()
        add_custom_target("Compile-${shaderfilename}-fragment" ALL
            COMMAND glslang-standalone "-I${ShaderPath}" "-V" ${Flags} "-o" "${FragmentShaderOutputPath}" "${FragmentShader}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FragmentShaderOutputPath}" "${ShaderBinOutputPath}/${FragmentShaderOutputFileName}")
    endif()

endforeach()

install(DIRECTORY Editor/Settings DESTINATION bin)
install(DIRECTORY Shaders/Cache DESTINATION bin/Shaders)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (UNIX AND NOT APPLE)
    install(DIRECTORY Editor/Settings DESTINATION Obelisk)
    install(DIRECTORY Shaders/Cache DESTINATION Obelisk/Shaders)
  else()
    install(DIRECTORY Editor/Settings DESTINATION Obelisk/${CMAKE_BUILD_TYPE})
    install(DIRECTORY Shaders/Cache DESTINATION Obelisk/${CMAKE_BUILD_TYPE}/Shaders)
  endif()
endif()

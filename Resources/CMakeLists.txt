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
set(ShaderBinOutputPath "${CMAKE_INSTALL_PREFIX}/bin/ZodiacEngine/Shaders/Cache")
if (UNIX AND NOT APPLE)
  set(ShaderObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/ZodiacEngine/Shaders/Cache")
else()
  set(ShaderObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/${CMAKE_BUILD_TYPE}/ZodiacEngine/Shaders/Cache")
endif()

set(SettingsSourcePath "${CMAKE_CURRENT_SOURCE_DIR}/Editor/Settings")
set(SettingsBinOutputPath "${CMAKE_INSTALL_PREFIX}/bin/ZodiacEngine/Settings")
if (UNIX AND NOT APPLE)
  set(SettingsObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/ZodiacEngine/Settings")
else()
  set(SettingsObeliskOutputPath "${CMAKE_INSTALL_PREFIX}/Obelisk/${CMAKE_BUILD_TYPE}/ZodiacEngine/Settings")
endif()

file (MAKE_DIRECTORY "${ShaderBinOutputPath}")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  file (MAKE_DIRECTORY "${ShaderObeliskOutputPath}")
  add_custom_target(CopyEngineSettings ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${SettingsSourcePath}" "${SettingsObeliskOutputPath}"
      COMMENT "Copying engine Settings to ZodiacEngine/")
endif()

file (GLOB VertexShaders CONFIGURE_DEPENDS "${ShaderPath}/*.vert")
file (GLOB FragmentShaders CONFIGURE_DEPENDS "${ShaderPath}/*.frag")
file (GLOB ComputeShaders CONFIGURE_DEPENDS "${ShaderPath}/*.comp")

# Editor executables load packaged SPIR-V. The aggregate target gives them a
# complete package, while output dependencies keep unchanged shaders out of
# subsequent builds.
add_custom_target(CompileShaders)

function(add_shader_compiler shader stage suffix)
    cmake_path(GET shader STEM shader_name)
    set(output_name "${shader_name}_${suffix}.spv")
    set(shader_output "${ShaderOutputPath}/${output_name}")
    set(packaged_outputs "${shader_output}" "${ShaderBinOutputPath}/${output_name}")
    set(shader_dependencies "${shader}")

    if (shader_name STREQUAL "sky_atmosphere_multiscattering")
        list(APPEND shader_dependencies "${ShaderPath}/sky_atmosphere_common.glsl")
    elseif (shader_name STREQUAL "sky_atmosphere_sky_view" OR shader_name STREQUAL "sky_atmosphere_aerial_perspective" OR shader_name STREQUAL "sky_atmosphere_source_radiance")
        list(APPEND shader_dependencies
            "${ShaderPath}/sky_atmosphere_common.glsl"
            "${ShaderPath}/sky_atmosphere_integrator.glsl")
    endif()

    add_custom_command(
        OUTPUT ${packaged_outputs}
        COMMAND $<TARGET_FILE:glslang-standalone> "-I${ShaderPath}" "-V" ${Flags} "-o" "${shader_output}" "${shader}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${shader_output}" "${ShaderBinOutputPath}/${output_name}"
        DEPENDS glslang-standalone ${shader_dependencies}
        VERBATIM
    )
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            OUTPUT "${ShaderObeliskOutputPath}/${output_name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${shader_output}" "${ShaderObeliskOutputPath}/${output_name}"
            DEPENDS "${shader_output}"
            VERBATIM
        )
        list(APPEND packaged_outputs "${ShaderObeliskOutputPath}/${output_name}")
    endif()

    add_custom_target("Compile-${shader_name}-${stage}" DEPENDS ${packaged_outputs})
    add_dependencies(CompileShaders "Compile-${shader_name}-${stage}")
endfunction()

foreach(shader ${VertexShaders})
    add_shader_compiler("${shader}" vertex vertex)
endforeach()

foreach(shader ${ComputeShaders})
    add_shader_compiler("${shader}" compute compute)
endforeach()

foreach(shader ${FragmentShaders})
    add_shader_compiler("${shader}" fragment fragment)
endforeach()

install(DIRECTORY Editor/Settings DESTINATION bin/ZodiacEngine)
install(DIRECTORY Shaders/Cache DESTINATION bin/ZodiacEngine/Shaders)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (UNIX AND NOT APPLE)
    install(DIRECTORY Editor/Settings DESTINATION Obelisk/ZodiacEngine)
    install(DIRECTORY Shaders/Cache DESTINATION Obelisk/ZodiacEngine/Shaders)
  else()
    install(DIRECTORY Editor/Settings DESTINATION Obelisk/${CMAKE_BUILD_TYPE}/ZodiacEngine)
    install(DIRECTORY Shaders/Cache DESTINATION Obelisk/${CMAKE_BUILD_TYPE}/ZodiacEngine/Shaders)
  endif()
endif()

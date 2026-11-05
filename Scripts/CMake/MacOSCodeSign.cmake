include_guard(GLOBAL)

# Register after the target's install(TARGETS) rule. Installation can rewrite
# Mach-O load commands after build-time signing; sign/verify the installed real
# file, not a symlink or the build-tree executable. No identity/notarization or
# hardened-runtime policy is implied by this local/CI ad-hoc signature.
function(zengine_sign_installed_target target destination)
    if(NOT APPLE)
        return()
    endif()
    install(CODE "
        set(_zengine_sign_file \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${destination}/$<TARGET_FILE_NAME:${target}>\")
        if(NOT EXISTS \"\${_zengine_sign_file}\" OR IS_SYMLINK \"\${_zengine_sign_file}\")
            message(FATAL_ERROR \"Installed signing target is missing or not a real file: \${_zengine_sign_file}\")
        endif()
        execute_process(COMMAND /usr/bin/codesign --force --sign - \"\${_zengine_sign_file}\"
            RESULT_VARIABLE _zengine_sign_result ERROR_VARIABLE _zengine_sign_error)
        if(NOT _zengine_sign_result EQUAL 0)
            message(FATAL_ERROR \"Failed to sign installed ${target}: \${_zengine_sign_error}\")
        endif()
        execute_process(COMMAND /usr/bin/codesign --verify --strict \"\${_zengine_sign_file}\"
            RESULT_VARIABLE _zengine_sign_result ERROR_VARIABLE _zengine_sign_error)
        if(NOT _zengine_sign_result EQUAL 0)
            message(FATAL_ERROR \"Invalid installed ${target} signature: \${_zengine_sign_error}\")
        endif()
    ")
endfunction()

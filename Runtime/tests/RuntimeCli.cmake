if(NOT EXISTS "${RUNTIME_EXECUTABLE}")
    message(FATAL_ERROR "Runtime executable is missing: ${RUNTIME_EXECUTABLE}")
endif()

execute_process(COMMAND "${RUNTIME_EXECUTABLE}" --help
    RESULT_VARIABLE help_result OUTPUT_VARIABLE help_output ERROR_VARIABLE help_error)
if(NOT help_result EQUAL 0 OR NOT help_output MATCHES "--cookedPackage" OR NOT help_output MATCHES "--cookedScene")
    message(FATAL_ERROR "Runtime help failed (${help_result}): ${help_error}")
endif()

foreach(scene IN ITEMS "not-a-uuid" "00000000-0000-0000-0000-000000000000")
    execute_process(COMMAND "${RUNTIME_EXECUTABLE}" --cookedPackage unused --cookedScene "${scene}"
        RESULT_VARIABLE invalid_result OUTPUT_VARIABLE invalid_output ERROR_VARIABLE invalid_error)
    if(NOT invalid_result EQUAL 2 OR NOT invalid_error MATCHES "non-nil scene UUID")
        message(FATAL_ERROR "Invalid scene '${scene}' was not rejected (${invalid_result}): ${invalid_output} ${invalid_error}")
    endif()
endforeach()

execute_process(COMMAND "${RUNTIME_EXECUTABLE}" --cookedPackage unused
    --cookedScene 10000000-0000-0000-0000-000000000001 --cookedAuditSource
    RESULT_VARIABLE audit_result OUTPUT_VARIABLE audit_output ERROR_VARIABLE audit_error)
if(NOT audit_result EQUAL 2 OR NOT audit_error MATCHES "test options must be valid")
    message(FATAL_ERROR "Invalid audit options were not rejected (${audit_result}): ${audit_output} ${audit_error}")
endif()

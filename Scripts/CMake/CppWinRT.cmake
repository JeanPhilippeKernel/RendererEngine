# Generates CppWinRT headers
#
#   generate_winrt_headers(
#       EXECUTABLE
#           <path to cppwinrt.exe to use>
#       INPUT
#           <inputs>*
#       OUTPUT
#           <output path>
#       [OPTIMIZE]
#
# Remarks:
#
function(generate_winrt_headers)
    set(FLAG_ARGS OPTIMIZE)
    set(ONE_VALUE_ARGS EXECUTABLE OUTPUT)
    set(MULTI_VALUE_ARGS INPUT)

    cmake_parse_arguments(CPPWINRT "${FLAG_ARGS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGV})

    list(APPEND CPPWINRT_ARGUMENTS -output ${CPPWINRT_OUTPUT})

    foreach(CPPWINRT_INPUT_ITEM ${CPPWINRT_INPUT})
        list(APPEND CPPWINRT_ARGUMENTS -input ${CPPWINRT_INPUT_ITEM})
    endforeach()

    if(CPPWINRT_OPTIMIZE)
        list(APPEND CPPWINRT_ARGUMENTS -optimize)
    endif()

    execute_process(
        COMMAND "${CPPWINRT_EXECUTABLE}" ${CPPWINRT_ARGUMENTS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND_ECHO STDOUT
        OUTPUT_VARIABLE WINRT_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endfunction()
# Applies PATCH to the source tree in the current directory once (safe on re-configure).
execute_process(COMMAND git apply --reverse --check --ignore-whitespace ${PATCH} RESULT_VARIABLE already
                OUTPUT_QUIET ERROR_QUIET)
if(already EQUAL 0)
    message(STATUS "patch already applied: ${PATCH}")
    return()
endif()
execute_process(COMMAND git apply --ignore-whitespace ${PATCH} RESULT_VARIABLE res)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "could not apply ${PATCH}")
endif()
message(STATUS "applied ${PATCH}")

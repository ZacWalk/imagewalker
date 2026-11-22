execute_process(COMMAND "${IW_GIT}" apply --ignore-space-change --check "${IW_PATCH}"
    RESULT_VARIABLE can_apply OUTPUT_QUIET ERROR_QUIET)
if(can_apply EQUAL 0)
    execute_process(COMMAND "${IW_GIT}" apply --ignore-space-change "${IW_PATCH}"
        COMMAND_ERROR_IS_FATAL ANY)
else()
    execute_process(COMMAND "${IW_GIT}" apply --ignore-space-change --reverse --check "${IW_PATCH}"
        RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_applied EQUAL 0)
        message(FATAL_ERROR "The pinned libdesktop palette patch did not apply: ${IW_PATCH}")
    endif()
endif()

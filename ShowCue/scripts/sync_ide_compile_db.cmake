# POST_BUILD: chỉ copy khi compile_commands.json đã có nội dung (sau cmake --generate).
if(NOT EXISTS "${CC_SRC}")
    message(STATUS "sync_ide_compile_db: compile_commands.json not yet generated — skip (will sync on next build)")
    return()
endif()

file(SIZE "${CC_SRC}" _cc_size)
if(_cc_size LESS 64)
    message(STATUS "sync_ide_compile_db: ${CC_SRC} is empty (${_cc_size} bytes) — skip copy")
    return()
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CC_SRC}" "${CC_DST}"
    RESULT_VARIABLE _copy_result)
if(NOT _copy_result EQUAL 0)
    message(WARNING "sync_ide_compile_db: could not copy to ${CC_DST}")
    return()
endif()

if(Python3_EXECUTABLE AND EXISTS "${DFLAGS_SCRIPT}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${DFLAGS_SCRIPT}" "${CC_SRC}" "${DFLAGS_OUT}"
        RESULT_VARIABLE _flags_result
        OUTPUT_VARIABLE _flags_out
        ERROR_VARIABLE _flags_err)
    if(NOT _flags_result EQUAL 0)
        message(WARNING "sync_ide_compile_db: compile_flags.txt failed: ${_flags_err}")
    else()
        message(STATUS "clangd: ${_flags_out}")
    endif()
endif()

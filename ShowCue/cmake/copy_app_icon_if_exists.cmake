if (NOT ICNS OR NOT DEST)
    message(FATAL_ERROR "copy_app_icon_if_exists.cmake requires ICNS and DEST")
endif()

if (NOT EXISTS "${ICNS}")
    return()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ICNS}" "${DEST}")

if (PNG_TO_REMOVE)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${PNG_TO_REMOVE}")
endif()

if(NOT DEFINED VIEWER)
    message(FATAL_ERROR "VIEWER is not defined")
endif()

if(NOT DEFINED MISSING_FILE)
    message(FATAL_ERROR "MISSING_FILE is not defined")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        QT_QPA_PLATFORM=invalid
        "${VIEWER}"
        --parse "${MISSING_FILE}"
    RESULT_VARIABLE parse_result
    OUTPUT_VARIABLE parse_stdout
    ERROR_VARIABLE parse_stderr
)

set(parse_output "${parse_stdout}${parse_stderr}")

if(NOT parse_result EQUAL 1)
    message(FATAL_ERROR "Expected parse mode to exit 1 for a missing file, got ${parse_result}")
endif()

if(NOT parse_output MATCHES "file does not exist")
    message(FATAL_ERROR "Expected missing-file message, got: ${parse_output}")
endif()

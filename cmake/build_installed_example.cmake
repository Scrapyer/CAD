if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is not defined")
endif()

if(NOT DEFINED PROJECT_BINARY_DIR)
    message(FATAL_ERROR "PROJECT_BINARY_DIR is not defined")
endif()

if(NOT DEFINED INSTALL_PREFIX)
    message(FATAL_ERROR "INSTALL_PREFIX is not defined")
endif()

if(NOT DEFINED EXAMPLE_BINARY_DIR)
    message(FATAL_ERROR "EXAMPLE_BINARY_DIR is not defined")
endif()

set(example_source_dir "${PROJECT_SOURCE_DIR}/examples/simple_viewer")
set(example_prefix_path "${INSTALL_PREFIX}")
if(DEFINED EXAMPLE_QT_PREFIX_PATH AND NOT EXAMPLE_QT_PREFIX_PATH STREQUAL "")
    list(APPEND example_prefix_path ${EXAMPLE_QT_PREFIX_PATH})
endif()
list(JOIN example_prefix_path ";" example_prefix_path_arg)
string(REPLACE ";" "\\;" example_prefix_path_arg "${example_prefix_path_arg}")

set(example_configure_args
    -S "${example_source_dir}"
    -B "${EXAMPLE_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${example_prefix_path_arg}"
)

if(DEFINED EXAMPLE_CMAKE_OSX_SYSROOT AND NOT EXAMPLE_CMAKE_OSX_SYSROOT STREQUAL "")
    list(APPEND example_configure_args
        "-DCMAKE_OSX_SYSROOT=${EXAMPLE_CMAKE_OSX_SYSROOT}"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${INSTALL_PREFIX}" "${EXAMPLE_BINARY_DIR}"
    RESULT_VARIABLE clean_result
)
if(NOT clean_result EQUAL 0)
    message(FATAL_ERROR "Failed to clean example test directories")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}" --prefix "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Failed to install FERender package")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${example_configure_args}
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Failed to configure simple_viewer example against installed FERender")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${EXAMPLE_BINARY_DIR}" -j 4
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Failed to build simple_viewer example against installed FERender")
endif()

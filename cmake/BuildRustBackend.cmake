cmake_minimum_required(VERSION 3.21)

foreach(required_var
    CARGO_EXECUTABLE
    CARGO_BIN_DIR
    CARGO_MANIFEST_PATH
    CARGO_WORKING_DIRECTORY
    CARGO_STAMP_FILE
)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable ${required_var}")
    endif()
endforeach()

if(WIN32)
    set(_path_separator ";")
else()
    set(_path_separator ":")
endif()

if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    set(ENV{PATH} "${CARGO_BIN_DIR}${_path_separator}$ENV{PATH}")
else()
    set(ENV{PATH} "${CARGO_BIN_DIR}")
endif()

set(_cargo_command
    "${CARGO_EXECUTABLE}"
    build
    --manifest-path
    "${CARGO_MANIFEST_PATH}"
)

if(DEFINED CARGO_ARGS AND NOT "${CARGO_ARGS}" STREQUAL "")
    string(REPLACE "__MATRIX_MEDIA_ARCHIVER_ARGSEP__" ";" _cargo_args "${CARGO_ARGS}")
    list(APPEND _cargo_command ${_cargo_args})
endif()

execute_process(
    COMMAND ${_cargo_command}
    WORKING_DIRECTORY "${CARGO_WORKING_DIRECTORY}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE _cargo_result
)

if(NOT _cargo_result EQUAL 0)
    message(FATAL_ERROR "cargo build failed with exit code ${_cargo_result}")
endif()

file(TOUCH "${CARGO_STAMP_FILE}")

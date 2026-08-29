# Download and locate Sparkle.framework for macOS bundle builds.
# Unsigned PR compiles only need the framework on the link/copy path;
# they do not call sign_update or notarytool.

set(MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION "2.9.6")
set(MATRIX_MEDIA_ARCHIVER_SPARKLE_URL
    "https://github.com/sparkle-project/Sparkle/releases/download/${MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION}/Sparkle-${MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION}.tar.xz"
)
set(MATRIX_MEDIA_ARCHIVER_SPARKLE_SHA256
    "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192"
)
set(MATRIX_MEDIA_ARCHIVER_SPARKLE_ARCHIVE
    "${CMAKE_BINARY_DIR}/sparkle/Sparkle-${MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION}.tar.xz"
)
set(MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/sparkle/extracted"
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sparkle")

if(NOT EXISTS "${MATRIX_MEDIA_ARCHIVER_SPARKLE_ARCHIVE}")
    message(STATUS "Downloading Sparkle ${MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION}")
    file(DOWNLOAD
        "${MATRIX_MEDIA_ARCHIVER_SPARKLE_URL}"
        "${MATRIX_MEDIA_ARCHIVER_SPARKLE_ARCHIVE}"
        EXPECTED_HASH SHA256=${MATRIX_MEDIA_ARCHIVER_SPARKLE_SHA256}
        TLS_VERIFY ON
        SHOW_PROGRESS
        STATUS _sparkle_download_status
    )
    list(GET _sparkle_download_status 0 _sparkle_download_code)
    if(NOT _sparkle_download_code EQUAL 0)
        list(GET _sparkle_download_status 1 _sparkle_download_message)
        message(FATAL_ERROR "Failed to download Sparkle: ${_sparkle_download_message}")
    endif()
endif()

if(NOT EXISTS "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}/Sparkle.framework")
    file(MAKE_DIRECTORY "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xvf "${MATRIX_MEDIA_ARCHIVER_SPARKLE_ARCHIVE}"
        WORKING_DIRECTORY "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}"
        RESULT_VARIABLE _sparkle_extract_result
    )
    if(NOT _sparkle_extract_result EQUAL 0)
        message(FATAL_ERROR "Failed to extract Sparkle archive")
    endif()
endif()

if(EXISTS "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}/Sparkle.framework")
    set(MATRIX_MEDIA_ARCHIVER_SPARKLE_FRAMEWORK
        "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}/Sparkle.framework"
    )
else()
    file(GLOB_RECURSE _sparkle_framework_candidates
        "${MATRIX_MEDIA_ARCHIVER_SPARKLE_EXTRACT_DIR}/**/Sparkle.framework"
    )
    if(NOT _sparkle_framework_candidates)
        message(FATAL_ERROR "Sparkle.framework not found after extracting Sparkle ${MATRIX_MEDIA_ARCHIVER_SPARKLE_VERSION}")
    endif()
    list(GET _sparkle_framework_candidates 0 MATRIX_MEDIA_ARCHIVER_SPARKLE_FRAMEWORK)
endif()

get_filename_component(MATRIX_MEDIA_ARCHIVER_SPARKLE_FRAMEWORK_PARENT
    "${MATRIX_MEDIA_ARCHIVER_SPARKLE_FRAMEWORK}" DIRECTORY
)
message(STATUS "Using Sparkle.framework at ${MATRIX_MEDIA_ARCHIVER_SPARKLE_FRAMEWORK}")

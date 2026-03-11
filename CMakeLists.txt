cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" MATRIX_MEDIA_ARCHIVER_APP_VERSION)
string(STRIP "${MATRIX_MEDIA_ARCHIVER_APP_VERSION}" MATRIX_MEDIA_ARCHIVER_APP_VERSION)

if(NOT MATRIX_MEDIA_ARCHIVER_APP_VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$")
    message(FATAL_ERROR "VERSION.txt must contain a version in year.month.day.build format")
endif()

project(MatrixMediaArchiverQt VERSION ${MATRIX_MEDIA_ARCHIVER_APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MATRIX_MEDIA_ARCHIVER_ENABLE_DEPLOYMENT "Run Qt deployment tooling during install" OFF)
option(MATRIX_MEDIA_ARCHIVER_BUILD_TESTS "Build and register Qt test targets" ON)
set(MATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET "" CACHE STRING
    "Optional Rust target triple for cross-compiling the backend sidecar"
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(MATRIX_MEDIA_ARCHIVER_MIN_QT_VERSION 6.2)

if(WIN32)
    # Cross-host Qt builds can spend excessive time in moc compiler predefines parsing.
    set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
endif()

include(GNUInstallDirs)

if(WIN32 AND MINGW)
    find_package(
        Qt6 ${MATRIX_MEDIA_ARCHIVER_MIN_QT_VERSION}
        REQUIRED
        COMPONENTS Core Gui Widgets Test
        OPTIONAL_COMPONENTS Sql
    )

    if(NOT TARGET Qt6::Sql)
        get_filename_component(MATRIX_MEDIA_ARCHIVER_QT_PREFIX
            "${Qt6_DIR}/../../.." ABSOLUTE
        )
        set(MATRIX_MEDIA_ARCHIVER_QTSQL_IMPLIB
            "${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/lib/libQt6Sql.a"
        )
        set(MATRIX_MEDIA_ARCHIVER_QTSQL_DLL
            "${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/bin/Qt6Sql.dll"
        )

        foreach(_qt_sql_path
            "${MATRIX_MEDIA_ARCHIVER_QTSQL_IMPLIB}"
            "${MATRIX_MEDIA_ARCHIVER_QTSQL_DLL}"
            "${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/include"
            "${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/include/QtSql"
        )
            if(NOT EXISTS "${_qt_sql_path}")
                message(FATAL_ERROR "Required Qt Sql artifact not found: ${_qt_sql_path}")
            endif()
        endforeach()
        unset(_qt_sql_path)

        add_library(Qt6::Sql SHARED IMPORTED)
        set_target_properties(Qt6::Sql PROPERTIES
            IMPORTED_IMPLIB "${MATRIX_MEDIA_ARCHIVER_QTSQL_IMPLIB}"
            IMPORTED_LOCATION "${MATRIX_MEDIA_ARCHIVER_QTSQL_DLL}"
            INTERFACE_COMPILE_DEFINITIONS QT_SQL_LIB
            INTERFACE_INCLUDE_DIRECTORIES
                "${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/include;${MATRIX_MEDIA_ARCHIVER_QT_PREFIX}/include/QtSql"
            INTERFACE_LINK_LIBRARIES Qt6::Core
        )
    endif()
else()
    find_package(Qt6 ${MATRIX_MEDIA_ARCHIVER_MIN_QT_VERSION} REQUIRED COMPONENTS Core Gui Widgets Sql Test)
endif()

find_program(CARGO_EXECUTABLE NAMES cargo HINTS /opt/homebrew/opt/rustup/bin)

if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo is required to build the Rust Matrix backend")
endif()

get_filename_component(CARGO_BIN_DIR "${CARGO_EXECUTABLE}" DIRECTORY)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup(REQUIRES ${MATRIX_MEDIA_ARCHIVER_MIN_QT_VERSION})
endif()

set(MATRIX_MEDIA_ARCHIVER_BACKEND_SOURCES
    backend/Cargo.lock
    backend/Cargo.toml
    backend/src/app_paths.rs
    backend/src/database.rs
    backend/src/domain.rs
    backend/src/folder_naming.rs
    backend/src/main.rs
    backend/src/media_classification.rs
    backend/src/protocol.rs
    backend/src/room_catalog.rs
    backend/src/secret_store.rs
    backend/src/service.rs
)

set(MATRIX_MEDIA_ARCHIVER_BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/backend)
set(MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_NAME matrix_media_archiver_backend)
if(WIN32)
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_NAME matrix_media_archiver_backend.exe)
endif()

if(DEFINED ENV{CARGO_TARGET_DIR} AND NOT "$ENV{CARGO_TARGET_DIR}" STREQUAL "")
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_TARGET_ROOT "$ENV{CARGO_TARGET_DIR}")
else()
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_TARGET_ROOT
        ${MATRIX_MEDIA_ARCHIVER_BACKEND_DIR}/target
    )
endif()

if(CMAKE_CONFIGURATION_TYPES)
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_PROFILE release)
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS --release)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release"
    OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
    OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_PROFILE release)
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS --release)
else()
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_PROFILE debug)
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS)
endif()

if(MATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET)
    list(APPEND MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS --target ${MATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET})
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_DIR
        ${MATRIX_MEDIA_ARCHIVER_BACKEND_TARGET_ROOT}/${MATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET}/${MATRIX_MEDIA_ARCHIVER_BACKEND_PROFILE}
    )
else()
    set(MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_DIR
        ${MATRIX_MEDIA_ARCHIVER_BACKEND_TARGET_ROOT}/${MATRIX_MEDIA_ARCHIVER_BACKEND_PROFILE}
    )
endif()

set(MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY
    ${MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_DIR}/${MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_NAME}
)
set(MATRIX_MEDIA_ARCHIVER_BACKEND_STAMP
    ${CMAKE_CURRENT_BINARY_DIR}/matrix_media_archiver_backend.stamp
)
string(JOIN "|" MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS_SERIALIZED ${MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS})

add_custom_command(
    OUTPUT ${MATRIX_MEDIA_ARCHIVER_BACKEND_STAMP}
    COMMAND ${CMAKE_COMMAND}
        "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
        "-DCARGO_BIN_DIR=${CARGO_BIN_DIR}"
        "-DCARGO_MANIFEST_PATH=${MATRIX_MEDIA_ARCHIVER_BACKEND_DIR}/Cargo.toml"
        "-DCARGO_WORKING_DIRECTORY=${MATRIX_MEDIA_ARCHIVER_BACKEND_DIR}"
        "-DCARGO_STAMP_FILE=${MATRIX_MEDIA_ARCHIVER_BACKEND_STAMP}"
        "-DCARGO_ARGS=${MATRIX_MEDIA_ARCHIVER_BACKEND_CARGO_ARGS_SERIALIZED}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildRustBackend.cmake
    DEPENDS ${MATRIX_MEDIA_ARCHIVER_BACKEND_SOURCES}
    WORKING_DIRECTORY ${MATRIX_MEDIA_ARCHIVER_BACKEND_DIR}
    COMMENT "Building Rust Matrix backend"
    VERBATIM
)

add_custom_target(MatrixMediaArchiverQtBackend ALL
    DEPENDS ${MATRIX_MEDIA_ARCHIVER_BACKEND_STAMP}
)

set(MATRIX_MEDIA_ARCHIVER_QT_SOURCES
    src/AppController.cpp
    src/AppController.h
    src/AppDatabase.cpp
    src/AppDatabase.h
    src/AppPaths.cpp
    src/AppPaths.h
    src/Domain.h
    src/FolderNaming.cpp
    src/FolderNaming.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/MatrixClientBackend.h
    src/MediaClassification.cpp
    src/MediaClassification.h
    src/ProcessMatrixClientBackend.cpp
    src/ProcessMatrixClientBackend.h
    src/SecretStore.cpp
    src/SecretStore.h
    src/main.cpp
)

qt_add_executable(MatrixMediaArchiverQt
    WIN32
    MACOSX_BUNDLE
    ${MATRIX_MEDIA_ARCHIVER_QT_SOURCES}
)

target_include_directories(MatrixMediaArchiverQt PRIVATE src)
add_dependencies(MatrixMediaArchiverQt MatrixMediaArchiverQtBackend)

target_link_libraries(MatrixMediaArchiverQt
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Sql
)

target_compile_definitions(MatrixMediaArchiverQt
    PRIVATE MATRIX_MEDIA_ARCHIVER_VERSION_STRING="${MATRIX_MEDIA_ARCHIVER_APP_VERSION}"
)

add_custom_command(TARGET MatrixMediaArchiverQt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY}
        $<TARGET_FILE_DIR:MatrixMediaArchiverQt>/${MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY_NAME}
    COMMENT "Copying Rust Matrix backend next to MatrixMediaArchiverQt"
)

install(TARGETS MatrixMediaArchiverQt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(NOT APPLE)
    install(PROGRAMS ${MATRIX_MEDIA_ARCHIVER_BACKEND_BINARY}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/MatrixMediaArchiverQt.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES packaging/linux/matrix-media-archiver.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()

if(MATRIX_MEDIA_ARCHIVER_ENABLE_DEPLOYMENT)
    qt_generate_deploy_app_script(
        TARGET MatrixMediaArchiverQt
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

if(MATRIX_MEDIA_ARCHIVER_BUILD_TESTS)
    qt_add_executable(MatrixMediaArchiverQtTests
        tests/PortUtilityTests.cpp
        src/Domain.h
        src/FolderNaming.cpp
        src/FolderNaming.h
        src/MediaClassification.cpp
        src/MediaClassification.h
    )

    target_include_directories(MatrixMediaArchiverQtTests PRIVATE src)

    target_link_libraries(MatrixMediaArchiverQtTests
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    enable_testing()
    add_test(NAME MatrixMediaArchiverQtTests COMMAND MatrixMediaArchiverQtTests)
endif()

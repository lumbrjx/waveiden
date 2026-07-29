cmake_minimum_required(VERSION 3.20)
project(waveiden VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WAVEIDEN_BUILD_CLI   "Build the CLI example"    ON)
option(WAVEIDEN_BUILD_TUI   "Build the animated terminal UI" ON)
option(WAVEIDEN_ENABLE_GRPC "Build the gRPC extension" OFF)

# ── SQLite amalgamation (no system sqlite-devel required) ─────────────────────
include(FetchContent)
FetchContent_Declare(
    sqlite3_amalgamation
    URL                    https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
    URL_HASH               SHA256=65230414820d43a6d1445d1d98cfe57e8eb9f7ac0d6a96ad6932e0647cce51db
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(sqlite3_amalgamation)

# Compile sqlite3.c directly into waveiden (avoids export-set issues with
# a separate static lib that CMake can't describe to downstream consumers).
set(SQLITE3_SRC ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c)
set(SQLITE3_INC ${sqlite3_amalgamation_SOURCE_DIR})

# ── libsndfile ────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# ── Core library: waveiden ────────────────────────────────────────────────────
add_library(waveiden STATIC
    src/dsp.cpp
    src/audio_reader.cpp
    src/fingerprint_engine.cpp
    src/sqlite_database.cpp
    ${SQLITE3_SRC})

target_include_directories(waveiden
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${SNDFILE_INCLUDE_DIRS}
            ${SQLITE3_INC})

target_link_libraries(waveiden
    PRIVATE ${SNDFILE_LIBRARIES})

target_compile_options(waveiden PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)

# ── CLI example ───────────────────────────────────────────────────────────────
if(WAVEIDEN_BUILD_CLI)
    set(WAVEIDEN_CLI_SOURCES examples/cli.cpp)
    if(WAVEIDEN_BUILD_TUI)
        pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
        pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
        list(APPEND WAVEIDEN_CLI_SOURCES examples/tui.cpp)
    endif()
    add_executable(waveiden_cli ${WAVEIDEN_CLI_SOURCES})
    # Keep the target name stable for downstream build scripts, while exposing
    # the product as the concise `waveiden` command to users.
    set_target_properties(waveiden_cli PROPERTIES OUTPUT_NAME waveiden)
    target_link_libraries(waveiden_cli PRIVATE waveiden)
    if(WAVEIDEN_BUILD_TUI)
        target_link_libraries(waveiden_cli PRIVATE PkgConfig::NCURSESW PkgConfig::PORTAUDIO)
        target_compile_definitions(waveiden_cli PRIVATE WAVEIDEN_HAS_TUI=1)
    endif()
endif()

# ── gRPC extension ────────────────────────────────────────────────────────────
if(WAVEIDEN_ENABLE_GRPC)
    find_package(protobuf REQUIRED)
    find_package(gRPC     REQUIRED)

    set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/grpc/proto/waveiden.proto")
    set(PROTO_OUT  "${CMAKE_CURRENT_BINARY_DIR}/grpc_generated")
    file(MAKE_DIRECTORY ${PROTO_OUT})

    get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)

    add_custom_command(
        OUTPUT
            ${PROTO_OUT}/waveiden.pb.cc
            ${PROTO_OUT}/waveiden.pb.h
            ${PROTO_OUT}/waveiden.grpc.pb.cc
            ${PROTO_OUT}/waveiden.grpc.pb.h
        COMMAND protobuf::protoc
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/grpc/proto
            --cpp_out=${PROTO_OUT}
            --grpc_out=${PROTO_OUT}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating protobuf/gRPC sources")

    add_library(waveiden_grpc STATIC
        ${PROTO_OUT}/waveiden.pb.cc
        ${PROTO_OUT}/waveiden.grpc.pb.cc
        grpc/src/waveiden_service.cpp)

    target_include_directories(waveiden_grpc
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/grpc/src>
            $<BUILD_INTERFACE:${PROTO_OUT}>
            $<INSTALL_INTERFACE:include/waveiden_grpc>)

    target_link_libraries(waveiden_grpc
        PUBLIC waveiden gRPC::grpc++ protobuf::libprotobuf)

    # Server example
    add_executable(waveiden_grpc_server examples/grpc_server.cpp)
    target_link_libraries(waveiden_grpc_server PRIVATE waveiden_grpc)

    # Client example (links only generated stubs + grpc++, no waveiden core needed)
    add_executable(waveiden_grpc_client examples/grpc_client.cpp)
    target_include_directories(waveiden_grpc_client PRIVATE ${PROTO_OUT})
    target_link_libraries(waveiden_grpc_client PRIVATE
        gRPC::grpc++ protobuf::libprotobuf)
    # Generated pb sources are already compiled into waveiden_grpc, but the
    # client needs them too as a standalone binary — recompile from the same files.
    target_sources(waveiden_grpc_client PRIVATE
        ${PROTO_OUT}/waveiden.pb.cc
        ${PROTO_OUT}/waveiden.grpc.pb.cc)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS waveiden
        EXPORT  waveidenTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(WAVEIDEN_BUILD_CLI)
    install(TARGETS waveiden_cli
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/waveiden
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT waveidenTargets
        FILE      waveidenTargets.cmake
        NAMESPACE waveiden::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/waveiden)

cmake_minimum_required(VERSION 3.10)

# The version lives in telnetty_core.h; read it so the two never drift.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/telnetty_core.h" _telnetty_version_line
     REGEX "^#define TELNETTY_VERSION_STRING \"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _telnetty_version "${_telnetty_version_line}")

project(Telnetty VERSION ${_telnetty_version} LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Building this project directly (not via add_subdirectory)?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TELNETTY_IS_TOP_LEVEL ON)
else()
    set(TELNETTY_IS_TOP_LEVEL OFF)
endif()

# Options
option(BUILD_EXAMPLES "Build example programs" ${TELNETTY_IS_TOP_LEVEL})
option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_DOCS "Build Doxygen API documentation (needs doxygen)" OFF)
option(ENABLE_ZLIB "Enable zlib compression support" ON)
option(ENABLE_DEBUG "Enable debug output and memory tracing" OFF)
option(BUILD_SUSPENDERS_EXAMPLE "Build the optional Memento/Suspenders server example" OFF)
set(MEMENTO_INCLUDE_DIR "" CACHE PATH "Directory containing memento.h")
set(SUSPENDERS_INCLUDE_DIR "" CACHE PATH "Directory containing suspenders.h")

if(TELNETTY_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(TELNETTY_HEADERS
    include/telnetty_core.h
    include/telnetty_events.h
    include/telnetty_buffer.h
    include/telnetty_options.h
    include/telnetty_mud.h
    include/telnetty_compression.h
    include/telnetty_mcp.h
    include/telnetty_color.h
)

# ---------------------------------------------------------------------------
# Library: header-only interface target
# ---------------------------------------------------------------------------

add_library(telnetty INTERFACE)
add_library(telnetty::telnetty ALIAS telnetty)
target_include_directories(telnetty INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/telnetty>
)
if(NOT CMAKE_VERSION VERSION_LESS 3.8)
    target_compile_features(telnetty INTERFACE c_std_99)
endif()

set(TELNETTY_WITH_ZLIB OFF)
if(ENABLE_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        set(TELNETTY_WITH_ZLIB ON)
        # Consumers need the define too, or compression silently compiles
        # out of their translation units.
        target_compile_definitions(telnetty INTERFACE TELNETTY_HAVE_ZLIB)
        target_link_libraries(telnetty INTERFACE ZLIB::ZLIB)
    endif()
endif()

# Warnings for the code this project builds itself (tests, examples).
set(TELNETTY_WARNINGS "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(TELNETTY_WARNINGS -Wall -Wextra -Wpedantic)
endif()

function(telnetty_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE telnetty::telnetty)
    target_compile_options(${name} PRIVATE ${TELNETTY_WARNINGS})
    if(ENABLE_DEBUG)
        target_compile_definitions(${name} PRIVATE TELNETTY_DEBUG_MEMORY)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Installation and package config (find_package(telnetty))
# ---------------------------------------------------------------------------

set(TELNETTY_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/telnetty)

install(FILES ${TELNETTY_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/telnetty)
install(TARGETS telnetty EXPORT telnetty-targets)
install(EXPORT telnetty-targets
    FILE telnetty-targets.cmake
    NAMESPACE telnetty::
    DESTINATION ${TELNETTY_CMAKE_DIR}
)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/telnetty-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/telnetty-config.cmake
    INSTALL_DESTINATION ${TELNETTY_CMAKE_DIR}
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/telnetty-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/telnetty-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/telnetty-config-version.cmake
    DESTINATION ${TELNETTY_CMAKE_DIR}
)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(BUILD_EXAMPLES)
    foreach(example server_simple client_simple)
        telnetty_program(${example} examples/${example}.c)
        # Socket APIs are POSIX, not C99.
        target_compile_definitions(${example} PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
        install(TARGETS ${example} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples)
    endforeach()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUV QUIET libuv)
        if(LIBUV_FOUND)
            telnetty_program(server_libuv examples/server_libuv.c)
            target_include_directories(server_libuv PRIVATE ${LIBUV_INCLUDE_DIRS})
            target_link_libraries(server_libuv PRIVATE ${LIBUV_LDFLAGS})
            install(TARGETS server_libuv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples)
        endif()
    endif()

    if(BUILD_SUSPENDERS_EXAMPLE)
        find_package(Threads REQUIRED)
        find_path(_MEMENTO_INCLUDE memento.h HINTS ${MEMENTO_INCLUDE_DIR})
        find_path(_SUSPENDERS_INCLUDE suspenders.h HINTS ${SUSPENDERS_INCLUDE_DIR})
        if(NOT _MEMENTO_INCLUDE OR NOT _SUSPENDERS_INCLUDE)
            message(FATAL_ERROR "BUILD_SUSPENDERS_EXAMPLE needs MEMENTO_INCLUDE_DIR and SUSPENDERS_INCLUDE_DIR")
        endif()
        add_executable(server_suspenders examples/server_suspenders.c)
        set_property(TARGET server_suspenders PROPERTY C_STANDARD 11)
        target_compile_definitions(server_suspenders PRIVATE _DEFAULT_SOURCE)
        target_include_directories(server_suspenders PRIVATE ${_MEMENTO_INCLUDE} ${_SUSPENDERS_INCLUDE})
        target_link_libraries(server_suspenders PRIVATE telnetty::telnetty Threads::Threads)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBURING QUIET liburing)
            if(LIBURING_FOUND)
                target_include_directories(server_suspenders PRIVATE ${LIBURING_INCLUDE_DIRS})
                target_link_libraries(server_suspenders PRIVATE ${LIBURING_LIBRARIES})
            endif()
        endif()
        install(TARGETS server_suspenders RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(BUILD_TESTS)
    enable_testing()

    foreach(test basic test_comprehensive test_qmethod test_protocols test_extended)
        telnetty_program(${test} tests/${test}.c)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()

    # A downstream define set (no explicit events implementation), built
    # as strict C99 with warnings as errors.
    telnetty_program(test_define_sets tests/test_define_sets.c)
    set_target_properties(test_define_sets PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_define_sets PRIVATE -Werror)
    endif()
    add_test(NAME test_define_sets COMMAND test_define_sets)

    # MANIAC Markov fuzz. Skipped when the generated matrices header is
    # absent; regenerate it with `python3 tests/maniac_gen.py`.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/maniac_matrices.h")
        telnetty_program(test_maniac tests/test_maniac.c)
        target_include_directories(test_maniac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME test_maniac COMMAND test_maniac)
    endif()

    # The examples over real sockets, driven by a scripted MUD client that
    # negotiates MCCP2/MCCP3 the way TinTin++ does.
    if(BUILD_EXAMPLES AND TELNETTY_WITH_ZLIB AND NOT WIN32)
        find_program(TELNETTY_PYTHON3 python3)
        if(TELNETTY_PYTHON3)
            add_test(NAME e2e_examples
                COMMAND ${TELNETTY_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_examples.py
                        ${CMAKE_CURRENT_BINARY_DIR})
            set_tests_properties(e2e_examples PROPERTIES TIMEOUT 120)
        endif()
    endif()

    # Header hygiene: every header must compile on its own, and all of them
    # together with every implementation enabled, as strict ISO C with no
    # POSIX feature macros and warnings as errors. This is what a downstream
    # project with -Wall -Wextra -Wpedantic -Werror sees.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(_check_dir ${CMAKE_CURRENT_BINARY_DIR}/header_check)
        set(_check_sources "")
        foreach(header ${TELNETTY_HEADERS})
            get_filename_component(_name ${header} NAME_WE)
            file(WRITE ${_check_dir}/${_name}.c
                "#include \"${_name}.h\"\nint ${_name}_check(void) { return 0; }\n")
            list(APPEND _check_sources ${_check_dir}/${_name}.c)
        endforeach()
        file(WRITE ${_check_dir}/all.c
            "#define TELNETTY_IMPLEMENTATION\n"
            "#define TELNETTY_EVENTS_IMPLEMENTATION\n"
            "#define TELNETTY_BUFFER_IMPLEMENTATION\n"
            "#define TELNETTY_OPTIONS_IMPLEMENTATION\n"
            "#define TELNETTY_MUD_IMPLEMENTATION\n"
            "#define TELNETTY_COMPRESSION_IMPLEMENTATION\n"
            "#define TELNETTY_COLOR_IMPLEMENTATION\n"
            "#define TELNETTY_MCP_IMPLEMENTATION\n"
            "#include \"telnetty_core.h\"\n#include \"telnetty_events.h\"\n"
            "#include \"telnetty_buffer.h\"\n#include \"telnetty_options.h\"\n"
            "#include \"telnetty_mud.h\"\n#include \"telnetty_compression.h\"\n"
            "#include \"telnetty_color.h\"\n#include \"telnetty_mcp.h\"\n"
            "int main(void) {\n"
            "    telnetty_context_t* ctx = telnetty_create(0, 0);\n"
            "    telnetty_destroy(ctx);\n"
            "    return 0;\n"
            "}\n")
        add_executable(test_header_hygiene ${_check_dir}/all.c ${_check_sources})
        target_link_libraries(test_header_hygiene PRIVATE telnetty::telnetty)
        set_target_properties(test_header_hygiene PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
        target_compile_options(test_header_hygiene PRIVATE ${TELNETTY_WARNINGS} -Werror)
        add_test(NAME test_header_hygiene COMMAND test_header_hygiene)

        # The headers carry extern "C" guards; check they really build as C++.
        include(CheckLanguage)
        check_language(CXX)
        if(CMAKE_CXX_COMPILER)
            enable_language(CXX)
            configure_file(${_check_dir}/all.c ${_check_dir}/all_cxx.cpp COPYONLY)
            add_executable(test_header_hygiene_cxx ${_check_dir}/all_cxx.cpp)
            target_link_libraries(test_header_hygiene_cxx PRIVATE telnetty::telnetty)
            set_target_properties(test_header_hygiene_cxx PROPERTIES CXX_STANDARD 11)
            target_compile_options(test_header_hygiene_cxx PRIVATE -Wall -Wextra -Werror)
            add_test(NAME test_header_hygiene_cxx COMMAND test_header_hygiene_cxx)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Documentation
# ---------------------------------------------------------------------------

if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_PROJECT_NAME "Telnetty")
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE README.md)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_PREDEFINED
        TELNETTY_IMPLEMENTATION TELNETTY_EVENTS_IMPLEMENTATION
        TELNETTY_BUFFER_IMPLEMENTATION TELNETTY_OPTIONS_IMPLEMENTATION
        TELNETTY_MUD_IMPLEMENTATION TELNETTY_COMPRESSION_IMPLEMENTATION
        TELNETTY_COLOR_IMPLEMENTATION TELNETTY_MCP_IMPLEMENTATION
        TELNETTY_HAVE_ZLIB TELNETTY_UNUSED=)
    doxygen_add_docs(doc ${TELNETTY_HEADERS} README.md
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen")
endif()

if(TELNETTY_IS_TOP_LEVEL)
    message(STATUS "Telnetty ${PROJECT_VERSION}")
    message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
    message(STATUS "  zlib (MCCP):    ${TELNETTY_WITH_ZLIB}")
    message(STATUS "  Examples:       ${BUILD_EXAMPLES}")
    message(STATUS "  Tests:          ${BUILD_TESTS}")
    message(STATUS "  Docs:           ${BUILD_DOCS}")
endif()

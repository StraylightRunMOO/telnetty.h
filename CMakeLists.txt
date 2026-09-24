cmake_minimum_required(VERSION 3.10)
project(Telnetty VERSION 1.1.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# strdup / strndup
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Options
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test programs" OFF)
option(ENABLE_ZLIB "Enable zlib compression support" ON)
option(ENABLE_DEBUG "Enable debug output" OFF)
option(BUILD_SUSPENDERS_EXAMPLE "Build the optional Memento/Suspenders server example" OFF)
set(MEMENTO_INCLUDE_DIR "" CACHE PATH "Directory containing memento.h")
set(SUSPENDERS_INCLUDE_DIR "" CACHE PATH "Directory containing suspenders.h")

# Find packages
set(TELNETTY_ZLIB_FOUND OFF)
if(ENABLE_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        set(TELNETTY_ZLIB_FOUND ON)
        add_definitions(-DTELNETTY_HAVE_ZLIB)
        include_directories(${ZLIB_INCLUDE_DIRS})
    endif()
endif()

# Compiler flags
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Suppress unused-function warnings for header-only library (expected behavior)
    add_compile_options(-Wno-unused-function)
    if(ENABLE_DEBUG)
        add_compile_options(-g -O0 -DTELNETTY_DEBUG_MEMORY)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Library headers (headers-only implementation)
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

# Create an interface library for the headers
add_library(telnetty INTERFACE)
target_include_directories(telnetty INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/telnetty>
)

if(TELNETTY_ZLIB_FOUND)
    target_link_libraries(telnetty INTERFACE ${ZLIB_LIBRARIES})
endif()

# Installation
install(FILES ${TELNETTY_HEADERS}
    DESTINATION include/telnetty
)

install(TARGETS telnetty
    EXPORT telnetty-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT telnetty-targets
    FILE telnetty-targets.cmake
    NAMESPACE telnetty::
    DESTINATION lib/cmake/telnetty
)

# Examples
if(BUILD_EXAMPLES)
    # Simple server example
    add_executable(server_simple examples/server_simple.c)
    if(TELNETTY_ZLIB_FOUND)
        target_link_libraries(server_simple ${ZLIB_LIBRARIES})
    endif()
    
    # Simple client example
    add_executable(client_simple examples/client_simple.c)
    if(TELNETTY_ZLIB_FOUND)
        target_link_libraries(client_simple ${ZLIB_LIBRARIES})
    endif()
    
    # Optional libuv-based server example
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUV libuv)
        if(LIBUV_FOUND)
            add_executable(server_libuv examples/server_libuv.c)
            target_link_libraries(server_libuv ${LIBUV_LIBRARIES} ${ZLIB_LIBRARIES})
            target_include_directories(server_libuv PRIVATE ${LIBUV_INCLUDE_DIRS})
        endif()
    endif()
    
    # Install examples
    install(TARGETS server_simple client_simple
        RUNTIME DESTINATION bin/examples
    )
    if(TARGET server_libuv)
        install(TARGETS server_libuv
            RUNTIME DESTINATION bin/examples
        )
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
        target_link_libraries(server_suspenders PRIVATE Threads::Threads)
        if(TELNETTY_ZLIB_FOUND)
            target_link_libraries(server_suspenders PRIVATE ${ZLIB_LIBRARIES})
        endif()
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBURING liburing)
            if(LIBURING_FOUND)
                target_include_directories(server_suspenders PRIVATE ${LIBURING_INCLUDE_DIRS})
                target_link_libraries(server_suspenders PRIVATE ${LIBURING_LIBRARIES})
            endif()
        endif()
        install(TARGETS server_suspenders RUNTIME DESTINATION bin/examples)
    endif()
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Basic functionality tests
    add_executable(test_basic tests/basic.c)
    if(TELNETTY_ZLIB_FOUND)
        target_link_libraries(test_basic ${ZLIB_LIBRARIES})
    endif()
    
    add_test(NAME test_basic COMMAND test_basic)
    
    # Comprehensive test suite
    add_executable(test_comprehensive tests/test_comprehensive.c)
    if(TELNETTY_ZLIB_FOUND)
        target_link_libraries(test_comprehensive ${ZLIB_LIBRARIES})
    endif()
    
    add_test(NAME test_comprehensive COMMAND test_comprehensive)

    add_executable(test_qmethod tests/test_qmethod.c)
    add_test(NAME test_qmethod COMMAND test_qmethod)

    add_executable(test_protocols tests/test_protocols.c)
    if(TELNETTY_ZLIB_FOUND)
        target_compile_definitions(test_protocols PRIVATE TELNETTY_HAVE_ZLIB)
        target_link_libraries(test_protocols ${ZLIB_LIBRARIES})
    endif()
    add_test(NAME test_protocols COMMAND test_protocols)

    # Extended-surface regression tests: 1.1 fixes, additive API, MTTS, color, 
    # MCP, extended events, RFC option round-trips, split-input torture.
    # Every new bug fix or protocol addition should land a case here.
    add_executable(test_extended tests/test_extended.c)
    if(TELNETTY_ZLIB_FOUND)
        target_compile_definitions(test_extended PRIVATE TELNETTY_HAVE_ZLIB)
        target_link_libraries(test_extended ${ZLIB_LIBRARIES})
    endif()
    add_test(NAME test_extended COMMAND test_extended)

    # MANIAC Markov fuzz. Skipped when the generated matrices header is
    # absent — regenerate with `python3 tests/maniac_gen.py`.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/maniac_matrices.h")
        add_executable(test_maniac tests/test_maniac.c)
        target_include_directories(test_maniac PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME test_maniac COMMAND test_maniac)
    endif()
endif()

# Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    
    add_custom_target(doc ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html
        DESTINATION share/doc/telnetty
    )
endif()

# Print configuration
message(STATUS "Telnetty Library Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Enable zlib: ${ENABLE_ZLIB}")
message(STATUS "  Enable debug: ${ENABLE_DEBUG}")
if(TELNETTY_ZLIB_FOUND)
    message(STATUS "  zlib found: ${ZLIB_VERSION_STRING}")
endif()
if(LIBUV_FOUND)
    message(STATUS "  libuv found: ${LIBUV_VERSION}")
endif()
